#ifndef HVoltage_H
#define HVoltage_H

#include <string>
#include <iostream>

#include "caen++/v6534.hpp"

#include "Tool.h"

class HVoltage: public ToolFramework::Tool {
  public:
    HVoltage();

    bool Initialise(std::string configfile, DataModel& data);
    bool Execute();
    bool Finalise();

    void connect();
    void configure();
 private:
    std::vector<caen::V6534> boards;

    ToolFramework::Logging& log(int level) {
      return *m_log << ToolFramework::MsgL(level, m_verbose);
    };

    ToolFramework::Logging& error() { return log(0); };
    ToolFramework::Logging& warn()  { return log(1); };
    ToolFramework::Logging& info()  { return log(2); };
};

#endif
