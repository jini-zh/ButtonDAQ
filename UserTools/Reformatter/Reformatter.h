#ifndef Reformatter_H
#define Reformatter_H

#include <string>
#include <iostream>

#include "Tool.h"

class Reformatter: public Tool {
  public:
    Reformatter();

    bool Initialise(std::string configfile, DataModel& data);
    bool Execute();
    bool Finalise();
};

#endif
