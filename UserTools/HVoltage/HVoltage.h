#ifndef HVoltage_H
#define HVoltage_H

#include <string>
#include <iostream>

#include "caen++/v6533.hpp"

#include "Tool.h"

class HVoltage: public Tool {
  public:
    HVoltage();

    bool Initialise(std::string configfile, DataModel& data);
    bool Execute();
    bool Finalise();

    void connect();
    void configure();
 private:
    std::vector<caen::V6533> boards;

    Logging& log(int level) { return *m_log << MsgL(level, m_verbose); };

    Logging& error() { return log(0); };
    Logging& warn()  { return log(1); };
    Logging& info()  { return log(2); };
};

#endif
