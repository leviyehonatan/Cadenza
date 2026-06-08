#pragma once

#include "Style.h"

#include <memory>
#include <mutex>
#include <optional>

namespace cadenza::arranger
{
class StyleChangeQueue
{
public:
    void publish(std::shared_ptr<const Style> style);
    std::optional<std::shared_ptr<const Style>> tryTake();
    void clear();

private:
    std::mutex m_mutex;
    std::optional<std::shared_ptr<const Style>> m_pending;
};
}
