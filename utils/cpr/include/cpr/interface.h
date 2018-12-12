#ifndef CPR_INTERFACE_H
#define CPR_INTERFACE_H

#include <string>

#include "cpr/defines.h"

namespace cpr {

class Interface {
  public:
    Interface(const std::string& eth): interface_{eth}{}

    const char* GetInterface() const noexcept;

  private:
    std::string interface_;
};

} // namespace cpr

#endif
