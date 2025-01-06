#include "DataModel.h"
#include "TimeSlice.h"

#include "Reformatter.h"

Reformatter::Reformatter(): Tool() {}

// See Table 2.3 in UM2580_DPSD_UserManual_rev9
static uint64_t time_from_seconds(long double seconds) {
  seconds /= 2e-9; // Tsampl
  uint64_t time = seconds; // Tcoarse
  seconds *= 1024;
  return time << 10
       | static_cast<uint64_t>(seconds) & 0x3ff; // Tfine
}

static long double time_to_seconds(uint64_t time) {
  return (
        static_cast<long double>(time >> 10)
      + static_cast<long double>(time & 0x3ff) / 1024.0L
  ) * 2e-9L;
}

inline static uint64_t decode_time(uint64_t time) {
  uint32_t tag    = time >> 32;
  uint32_t extras = time   & 0xffffffff;
  uint64_t result = extras & 0xffff0000; // bits 16 to 31
  result <<= 31 - 16;
  result |= tag;
  result <<= 10;
  result |= extras & 0x3ff; // bits 0 to 9
  return result;
}

// CAENDigitizer 2.17.3 coupled with DPP-PSD firmware version 136.137 (AMC)
// 04.25 (ROC) has a bug when the baseline (times 4) is returned as an int16_t
// rather than uint16_t, with the sign depending on the channel pulse polarity.
// This function decodes the proper baseline value.
inline static uint16_t decode_baseline(uint16_t baseline) {
  // XXX: currently assuming negative pulse polarity
#if 0
  // positive pulse polarity
  return (uint16_t)-baseline / 4;
#else
  // negative pulse polarity
  return baseline / 4;
#endif
}

void Reformatter::send_timeslice(uint64_t time, std::vector<Hit>& hits) {
  if (hits.empty()) return;

  std::unique_ptr<TimeSlice> timeslice(new TimeSlice);
  timeslice->time = time;
  timeslice->hits.insert(
      timeslice->hits.begin(),
      std::make_move_iterator(hits.begin()),
      std::make_move_iterator(hits.end())
  );
  hits.clear();

  std::lock_guard<std::mutex> lock(m_data->readout_mutex);
  m_data->readout.push(std::move(timeslice));
};

template <typename Container>
static void remove_if(
    Container& c,
    const std::function<bool (typename Container::value_type&)>& predicate
) {
  c.erase(std::remove_if(c.begin(), c.end(), predicate), c.end());
};

void Reformatter::reformat() {
  /* We wait until the time of the earlist hit available for processing across
   * all channels (`start`) plus the desired timeslice length (`interval`) is
   * less than the time of the latest hit in the channel for all active
   * channels. A channel is active if we have seen data from it not too long
   * ago. When we have all the hits we need, they are extracted from `readouts`
   * and separated into `current` and `next` with `current` storing the hits
   * fitting the time window, and `next` storing the hits to be send next.
   * `current` data is then sent for processing.
   */

  uint64_t start = 0;
  uint64_t time  = 0;

  while (!stop) {
    if (m_data->raw_readout) {
      {
        std::lock_guard<std::mutex> lock(m_data->raw_readout_mutex);
        readouts.push_back(std::move(m_data->raw_readout));
      };

      for (auto& board : *readouts.back())
        for (auto& hit : *board) {
          // Decode CAEN data format
          hit.time     = decode_time(hit.time);
          hit.baseline = decode_baseline(hit.baseline);

          if (hit.channel >= channels.size()) {
            std::stringstream ss;
            ss << "Unexpected hit channel " << static_cast<int>(hit.channel);
            throw std::runtime_error(ss.str());
          };

          Channel& channel = channels[hit.channel];
          if (channel.active) {
            if (hit.time > channel.time) channel.time = hit.time;
          } else {
            channel.active = true;
            channel.time = hit.time;
          };
        };
    } else {
      usleep(time_to_seconds(interval) * 0.5e6);
      continue;
    };

    for (auto& channel : channels)
      if (channel.time > time)
        time = channel.time;

    while (true) {
      uint64_t end = start + interval;
      if (time < end) break;

      bool done = false;
      for (auto& channel : channels)
        if (channel.active && channel.time < end)
          if (channel.time + dead_time > time) {
            // some channel may yet provide data fitting the current time
            // window
            done = true;
            break;
          } else {
            channel.active = false;
            done = done && channel.time >= start;
          };
      if (done) break;

      remove_if(
          readouts,
          [this, end](
            std::unique_ptr<
              std::list<std::unique_ptr<std::vector<Hit>>>
            >& readout
          ) -> bool {
            remove_if(
                *readout,
                [this, end](std::unique_ptr<std::vector<Hit>>& board) -> bool {
                  remove_if(
                      *board,
                      [this, end](Hit& hit) -> bool {
                        if (hit.time >= end) return false;
                        buffer.push_back(std::move(hit));
                        return true;
                      }
                  );
                  return board->empty();
                }
            );
            return readout->empty();
          }
      );

      send_timeslice(start, buffer);

      start = end;
    };
  };
};

bool Reformatter::Initialise(std::string configfile, DataModel& data) {
  InitialiseTool(data);
  InitialiseConfiguration(configfile);

  if (!m_variables.Get("verbose", m_verbose)) m_verbose = 1;

  long double time = 0.1;
  m_variables.Get("interval", time);
  if (time <= 0) {
    *m_data->Log
      << ML(0) << "Reformatter: invalid time interval: " << time
      << ", using 0.1 s" << std::endl;
    time = 0.1;
  };
  interval = time_from_seconds(time);

  dead_time = 10 * interval;
  if (m_variables.Get("dead_time", time)) {
    if (time <= 0) {
      *m_data->Log << ML(0) << "Reformatter: invalid dead time: " << time;
      time = 10 * time_to_seconds(interval);
      *m_data->Log << ", using " << time << " s" << std::endl;
    };
    dead_time = time_from_seconds(time) + interval;
  };

  channels.resize(m_data->enabled_digitizer_channels.size() * 16);
  {
    int i = 0;
    for (uint16_t mask : m_data->enabled_digitizer_channels)
      for (int j = 0; j < 16; ++j) {
        auto& channel = channels[i++];
        channel.time   = 0;
        channel.active = mask & 1 << j;
      };
  };

  stop = false;
  thread = std::thread(&Reformatter::reformat, this);

  ExportConfiguration();
  return true;
}

bool Reformatter::Execute() {
  return true;
}

bool Reformatter::Finalise() {
  if (!stop) {
    stop = true;
    thread.join();
  };
  return true;
}
