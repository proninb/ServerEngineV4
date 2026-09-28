#include "subscription_registration.hpp"

#include <utility>

namespace cw::server {

subscription_registration::subscription_registration(
    void* value_context,
    unregister_function value_unregister) noexcept
    : context(value_context),
      unregister(value_unregister) {
}

subscription_registration::subscription_registration(
    subscription_registration&& other) noexcept
    : context(std::exchange(other.context, nullptr)),
      unregister(std::exchange(other.unregister, nullptr)) {
}

subscription_registration&
subscription_registration::operator=(
    subscription_registration&& other) noexcept {

    if (this != &other) {
        reset();
        context =
            std::exchange(other.context, nullptr);
        unregister =
            std::exchange(other.unregister, nullptr);
    }

    return *this;
}

subscription_registration::~subscription_registration() {
    reset();
}

void subscription_registration::reset() noexcept {
    if (unregister != nullptr) {
        unregister(context);
    }

    context = nullptr;
    unregister = nullptr;
}

subscription_registration::operator bool() const noexcept {
    return unregister != nullptr;
}

}
