#include "DataModel.h"
#include "TimeSlice.h"

#include "Reformatter.h"

Reformatter::Reformatter(): Tool() {}

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

void Reformatter::send_timeslice(Time time, std::vector<Hit>& hits) {
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

  Time start;
  Time time;

  while (!stop) {
    if (m_data->raw_readout) {
      {
        std::lock_guard<std::mutex> lock(m_data->raw_readout_mutex);
        readouts.push_back(std::move(m_data->raw_readout));
      };

      for (auto& board : *readouts.back())
        for (auto& hit : *board) {
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
      usleep(interval.seconds() * 0.5e6);
      continue;
    };

    for (auto& channel : channels)
      if (channel.time > time)
        time = channel.time;

    while (true) {
      Time end = start + interval;
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
  interval = Time(time);

  dead_time = 10 * interval;
  if (m_variables.Get("dead_time", time)) {
    if (time <= 0) {
      *m_data->Log << ML(0) << "Reformatter: invalid dead time: " << time;
      time = 10 * interval.seconds();
      *m_data->Log << ", using " << time << " s" << std::endl;
    };
    dead_time = Time(time) + interval;
  };

  channels.resize(m_data->enabled_digitizer_channels.size() * 16);
  {
    int i = 0;
    for (uint16_t mask : m_data->enabled_digitizer_channels)
      for (int j = 0; j < 16; ++j)
        channels[i++].active = mask & 1 << j;
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
