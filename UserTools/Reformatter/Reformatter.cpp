#include "Reformatter.h"
#include "TimeSlice.h"

Reformatter::Reformatter(): Tool() {}

// See Table 2.3 in UM2580_DPSD_UserManual_rev9
static uint64_t time_from_seconds(long double seconds) {
  seconds /= 2e-9; // Tsampl
  uint64_t time = seconds; // Tcoarse
  seconds *= 1024;
  return time << 10
       | static_cast<uint64_t>(seconds) & 0x3ff; // Tfine
};

static long double time_to_seconds(uint64_t time) {
  return (
        static_cast<long double>(time >> 10)
      + static_cast<long double>(time & 0x3ff) / 1024.0L
  ) * 2e-9L;
};

inline static uint64_t decode_time(uint64_t time) {
  uint32_t tag    = time >> 32;
  uint32_t extras = time   & 0xffffffff;
  uint64_t result = extras & 0xffff0000; // bits 16 to 31
  result <<= 31 - 16;
  result |= tag;
  result <<= 10;
  result |= extras & 0x3ff; // bits 0 to 9
  return result;
};

inline static uint16_t decode_baseline(uint16_t baseline) {
  return (uint16_t)-baseline / 4;
};

void Reformatter::ThreadArgs::send(const std::vector<Hit>& hits) {
  std::unique_ptr<TimeSlice> timeslice(new TimeSlice);
  timeslice->hits = hits;
  std::lock_guard<std::mutex> lock(tool.m_data->readout_mutex);
  tool.m_data->readout.push(std::move(timeslice));
};

void Reformatter::ThreadArgs::execute() {
  if (!tool.m_data->raw_readout) return;

  /* We wait until the following condition is true for each channel: the time
   * of the earliest hit available for processing across all channels (`time`)
   * plus the desired timeslice length (`interval`) is less than the time of
   * the latest hit in the channel.  After that the hits are extracted from
   * `readouts` and separated into `current` and `next` with `current` storing
   * hits fitting the time window, and `next` storing the hits to be processed
   * later. */

  {
    std::lock_guard<std::mutex> lock(tool.m_data->raw_readout_mutex);
    readouts.push_back(std::move(tool.m_data->raw_readout));
  };

  for (auto& board : *readouts.back())
    for (auto& hit : *board) {
      hit.time     = decode_time(hit.time);
      hit.baseline = decode_baseline(hit.baseline);

      if (hit.channel >= channels.size()) channels.resize(hit.channel + 1);
      Channel& channel = channels[hit.channel];
      if (channel.active) {
        if (hit.time < channel.min)
          channel.min = hit.time;
        else if (hit.time > channel.max)
          channel.max = hit.time;
      } else {
        channel.active = true;
        channel.min = channel.max = hit.time;
      };
    };

  for (auto& channel : channels)
    if (channel.active) {
      if (channel.min < time_min)
        time_min = channel.min;
      else if (channel.max > time_max)
        time_max = channel.max;
    };

  // check the time window
  uint64_t end = time_min + tool.interval;
  uint64_t deadline = time_max > tool.dead_time ? time_max - tool.dead_time : 0;

  *tool.m_log << MsgL(2, 2) << time_to_seconds(deadline);
  for (size_t i = 0; i < channels.size(); ++i)
    *tool.m_log
      << ' '
      << i
      << ' '
      << channels[i].active
      << ' '
      << time_to_seconds(channels[i].max);
  *tool.m_log << std::endl;

  for (auto& channel : channels)
    if (channel.max < end && channel.max > deadline)
      // some channel may have more data in the future
      return;

  for (auto& channel : channels)
    if (channel.max < deadline)
      channel.active = false;
    else
      channel.min = channel.max;

  if (next_max < end)
    // all of the events from the previous iteration fit the time window
    std::swap(current, next);
  else {
    auto inext = next->begin();
    for (auto& hit : *next)
      if (hit.time < end)
        current->push_back(std::move(hit));
      else {
        *inext++ = std::move(hit);
        channels[hit.channel].min = std::min(
            channels[hit.channel].min, hit.time
        );
      };
    next->resize(inext - next->begin());
  };

  // Prepare the timeslice hits. Store events hitting the time window in
  // current, the rest in next.
  for (auto& readout : readouts)
    for (auto& board : *readout)
      for (auto& hit : *board)
        if (hit.time < end)
          current->push_back(std::move(hit));
        else {
          next->push_back(std::move(hit));
          channels[hit.channel].min = std::min(
              channels[hit.channel].min, hit.time
          );
        };
  readouts.clear();

  // Copy the hits and send the timeslice
  send(*current);
  current->clear();

  time_min = std::numeric_limits<uint64_t>().max();
  for (auto& channel : channels)
    if (channel.active) {
      time_min = std::min(time_min, channel.min);
      next_max = std::max(next_max, channel.max);
    };

  return;
};

Reformatter::ThreadArgs::~ThreadArgs() {
  // Send the last hits for processing
  if (!next->empty()) send(*next);
};

void Reformatter::Thread(Thread_args* args) {
  static_cast<ThreadArgs*>(args)->execute();

  auto a = static_cast<ThreadArgs*>(args);
  DataModel* data = a->tool.m_data;
  while (!data->readout.empty()) {
    std::unique_ptr<TimeSlice> timeslice;
    {
      std::lock_guard<std::mutex> lock(data->readout_mutex);
      timeslice = std::move(data->readout.front());
      data->readout.pop();
    };

    *a->tool.m_log
      << MsgL(2, 2)
      << timeslice->hits.size()
      << '\t'
      << timeslice->hits.front().time
      << " ("
      << time_to_seconds(timeslice->hits.front().time)
      << ")\t"
      << timeslice->hits.back().time
      << " ("
      << time_to_seconds(timeslice->hits.back().time)
      << ')'
      << std::endl;
  };
};

bool Reformatter::Initialise(std::string configfile, DataModel& data) {
  if (configfile != "") m_variables.Initialise(configfile);
  //m_variables.Print();

  m_data = &data;
  m_log  = m_data->Log;

  if (!m_variables.Get("verbose", m_verbose)) m_verbose = 1;

  long double time = 0.1;
  m_variables.Get("interval", time);
  interval = time_from_seconds(time);

  time = 1;
  m_variables.Get("dead_time", time);
  dead_time = time_from_seconds(time);

  thread = new ThreadArgs(*this);
  util.CreateThread("Reformatter", &Thread, thread);

  return true;
}

bool Reformatter::Execute() {
  sleep(1);
  return true;
};

bool Reformatter::Finalise() {
  util.KillThread(thread);
  delete thread;
  return true;
}
