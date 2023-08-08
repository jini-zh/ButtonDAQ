#ifndef TIME_SLICE_H
#define TIME_SLICE_H

#include <vector>
#include <map>
#include <mutex>
#include <type_traits>

#include <cstddef>

#include "Hit.h"

enum class trigger_type {nhits, calib, zero_bais};  

class TimeSlice {
  private:
    template <bool is_const = false>
    class iterator_ {
      public:
        using value_type        = Hit;
        using difference_type   = ptrdiff_t;
        using reference
          = typename std::conditional<is_const, const Hit&, Hit&>::type;
        using pointer
          = typename std::conditional<is_const, const Hit*, Hit*>::type;
        using iterator_category = std::forward_iterator_tag;

        iterator_(): hit(nullptr) {};
        iterator_(pointer hit): hit(hit) {};
        iterator_(const iterator_<is_const>& i): hit(i.hit) {};

        template <
          bool is_const_ = is_const,
          typename = typename std::enable_if<is_const_>::type
        >
        iterator_(iterator_<false> i): hit(&*i) {};

        iterator_& operator=(const iterator_& i) {
          hit = i.hit;
          return *this;
        };

        bool operator==(const iterator_& i) const {
          return hit == i.hit;
        };

        bool operator!=(const iterator_& i) const {
          return hit != i.hit;
        };

        reference operator*() {
          return *hit;
        };

        pointer operator->() {
          return hit;
        };

        iterator_& operator++() {
          add1(1);
          return *this;
        };

        iterator_ operator++(int) {
          iterator_ i(*this);
          add1(1);
          return i;
        };

      private:
        pointer hit;

        void add1(int sign) {
          if (hit->has_waveform)
            hit = reinterpret_cast<Hit*>(
                reinterpret_cast<uint8_t*>(hit)
                + sign * hit->nsamples() * sizeof(uint16_t)
            );
          else
            hit += sign;
        };
    };

    Hit* hits = nullptr;
    size_t size = 0;

  public:
    using iterator       = iterator_<false>;
    using const_iterator = iterator_<true>;

    std::mutex mutex; 
    std::vector<std::pair<trigger_type, unsigned long>> positive_trggers;
    std::map<trigger_type, bool> trigger_flags;

    TimeSlice(const std::vector<CAENEvent>&);
    ~TimeSlice();

    const_iterator begin() const { return const_iterator(hits); };
    iterator       begin()       { return iterator(hits); };

    iterator end() {
      return reinterpret_cast<Hit*>(
          reinterpret_cast<uint8_t*>(hits) + size
      );
    }

    const_iterator end() const {
      return const_iterator(const_cast<TimeSlice*>(this)->end());
    };
};

#endif
