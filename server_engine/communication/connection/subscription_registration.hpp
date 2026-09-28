/*
 * Move-only publisher registration owned by one connection subscription.
 */
#pragma once

namespace cw::server {

class subscription_registration final {
public:
    using unregister_function =
        void (*)(void* context) noexcept;

    subscription_registration() noexcept = default;

    subscription_registration(
        void* context,
        unregister_function unregister) noexcept;

    subscription_registration(subscription_registration&& other) noexcept;
    subscription_registration& operator=(subscription_registration&& other) noexcept;
    ~subscription_registration();

    subscription_registration(const subscription_registration&) = delete;
    subscription_registration& operator=(const subscription_registration&) = delete;

    void reset() noexcept;
    [[nodiscard]] explicit operator bool() const noexcept;

private:
    void* context = nullptr;
    unregister_function unregister = nullptr;
};

}
