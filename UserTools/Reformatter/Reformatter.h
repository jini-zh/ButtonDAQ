#ifndef Reformatter_H
#define Reformatter_H

#include <string>
#include <iostream>

#include "Tool.h"

class Reformatter: public ToolFramework::Tool {
  public:
    Reformatter();
    ~Reformatter() { Finalise(); };

    bool Initialise(std::string configfile, DataModel& data);
    bool Execute();
    bool Finalise();

  private:
    struct Channel {
      // timespan of hits that haven't been formed into a timeslice yet
      uint64_t min = 0;
      uint64_t max = 0;

      // we have or expect to have data in this channel
      // (we have seen events coming from this channel and the channel
      // digitizer is not marked as inactive)
      bool active;
    };

    std::unique_ptr<std::vector<Hit>> current;
    std::unique_ptr<std::vector<Hit>> next;

    std::vector<
      std::unique_ptr<
        std::list<
          std::unique_ptr<
            std::vector<Hit>
          >
        >
      >
    > readouts;

    std::vector<Channel> channels;

    // target timeslice length
    uint64_t interval;

    // max time to wait for data from a channel
    uint64_t dead_time;

    // last timeslice target timestamp
    uint64_t time = 0;

    bool stop = true;
    std::thread thread;

    void send_timeslice(std::vector<Hit>& hits);
    void reformat();
};

#endif
