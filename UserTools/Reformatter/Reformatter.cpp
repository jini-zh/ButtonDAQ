#include "Reformatter.h"
#include "TimeSlice.h"

Reformatter::Reformatter(): Tool() {}

bool Reformatter::Initialise(std::string configfile, DataModel& data) {
  if (configfile != "") m_variables.Initialise(configfile);
  //m_variables.Print();

  m_data = &data;
  m_log  = m_data->Log;

  if (!m_variables.Get("verbose",m_verbose)) m_verbose = 1;
  
  return true;
}

bool Reformatter::Execute() {
  std::unique_ptr<std::vector<CAENEvent>> readout;
  {
    std::lock_guard<std::mutex> lock(m_data->raw_readout_mutex);
    if (m_data->raw_readout_queue.empty()) return true;
    readout = std::move(m_data->raw_readout_queue.front());
    m_data->raw_readout_queue.pop();
  };

  m_data->readout_queue.emplace(new TimeSlice(*readout));

  return true;
}

bool Reformatter::Finalise() {
  return true;
}
