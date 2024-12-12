#ifndef Reformatter_H
#define Reformatter_H

#include <string>
#include <iostream>

#include "Tool.h"

class Reformatter: public ToolFramework::Tool {
  public:
    Reformatter();

    bool Initialise(std::string configfile, DataModel& data);
    bool Execute();
    bool Finalise();

  private:
    struct ThreadArgs : Thread_args {
      struct Channel {
        // hit times available in next or readout
        uint64_t min;
        uint64_t max;

        // we have data from this channel in next or readout
        bool active;
      };

      Reformatter& tool;

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

      // time of the earliest hit in next or readout (min(channels.min))
      uint64_t time_min = std::numeric_limits<uint64_t>().max();
      // time of the latest hit in next or readout (max(channels.max))
      uint64_t time_max = 0;

      // time of the latest hit in next
      uint64_t next_max;

      ThreadArgs(Reformatter& tool):
        tool(tool),
        current(new std::vector<Hit>()),
        next(new std::vector<Hit>())
      {};

      ~ThreadArgs();

      void send(const std::vector<Hit>& hits);
      void execute();
    };

    // target timeslice length
    uint64_t interval;
    // we stop waiting for channels that have had no data this time long
    uint64_t dead_time;

    Utilities util;
    ThreadArgs* thread;

    static void Thread(Thread_args*);
};

#endif
