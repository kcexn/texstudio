#pragma once
#ifndef Header_Debouncer
#define Header_Debouncer
#include <QObject>
#include <QTimer>
#include <QVariant>

// --- FUNCTION TRAITS ---

// Primary template
template<typename T>
struct function_traits;

// Specialization for lambdas, functors, and std::function
template<typename T>
struct function_traits : function_traits<decltype(&T::operator())> {};

// Specialization for const member function pointers (used for lambdas)
template<typename ClassType, typename ReturnType, typename... Args>
struct function_traits<ReturnType(ClassType::*)(Args...) const> {
    using signature = std::function<ReturnType(Args...)>;
};

// Specialization for non-const member function pointers
template<typename ClassType, typename ReturnType, typename... Args>
struct function_traits<ReturnType(ClassType::*)(Args...)> {
    using signature = std::function<ReturnType(Args...)>;
};

// Specialization for free function pointers
template<typename ReturnType, typename... Args>
struct function_traits<ReturnType(*)(Args...)> {
    using signature = std::function<ReturnType(Args...)>;
};

// --- COMPILE-TIME VALIDATION ---

template<typename T, typename = void>
struct is_valid_callable : std::false_type {};

template<typename T>
struct is_valid_callable<T, std::void_t<typename function_traits<T>::signature>> : std::true_type {};

/*!
 * \brief Creates a debounced version of a slot or callable.
 *
 * This function returns a new callable (a lambda) that, when invoked,
 * will delay the execution of the original function `func`. The execution is
 * delayed until the returned callable has not been invoked for `durationMs`
 * milliseconds.
 *
 * This is useful for signals that fire rapidly (e.g., `textChanged`, `mouseMove`)
 * to prevent the connected slot from running too frequently.
 *
 * \tparam Func The type of the callable (e.g., function pointer, lambda).
 * \tparam ...Args The argument types of the callable `f`.
 * \param[in] func The function/slot to debounce. It must be a callable that can be
 * wrapped in `std::function`.
 * \param[in] context The `QObject` that will own the internal `QTimer`. This is crucial
 * for the timer's lifetime management.
 * \param[in] durationMs The debounce delay in milliseconds.
 * \return A new lambda function that you can connect to a signal.
 *
 * \note The lifetime of the internal `QTimer` is tied to the `context` object.
 * If the `context` object is destroyed, the debounced function will
 * cease to work.
 *
 * \code
 * // Example usage in a QWidget
 * QLineEdit *lineEdit = new QLineEdit(this);
 * QLabel *label = new QLabel(this);
 *
 * // Create a debounced function that updates a label
 * auto debouncedUpdate = debounce([label](const QString &text) {
 * label->setText("You typed: " + text);
 * }, this, 500); // Debounce for 500ms
 *
 * // Connect the textChanged signal to our debounced function
 * connect(lineEdit, &QLineEdit::textChanged, debouncedUpdate);
 * \endcode
 */
template <typename Func>
typename function_traits<std::decay_t<Func>>::signature
debounce(Func&& func, QObject* context, int durationMs = 300)
{
    static_assert(is_valid_callable<std::decay_t<Func>>::value,
                  "debounce() requires a callable type like a lambda, functor, or function pointer.");
    assert(context != nullptr && "Context object cannot be null.");
    assert(durationMs >= 0 && "Duration must be non-negative.");

    auto shared_func = std::make_shared<std::decay_t<Func>>(std::forward<Func>(func));

    // This static counter ensures that each call to debounce gets a unique ID,
    // preventing collisions when creating multiple debounced functions.
    static std::size_t debounce_counter = 0;
    const std::string timerPropertyName =
        "_debounce_timer_" + std::to_string(reinterpret_cast<uintptr_t>(context)) + "_" + std::to_string(debounce_counter++);

    return [shared_func, context, durationMs, timerPropertyName]
        (auto&&... args) mutable
    {
        QVariant timerVariant = context->property(timerPropertyName.c_str());
        auto *timer = qobject_cast<QTimer*>(timerVariant.value<QObject*>());

        if (!timer) {
            timer = new QTimer(context);
            timer->setInterval(durationMs);
            timer->setSingleShot(true);

            context->setProperty(timerPropertyName.c_str(), QVariant::fromValue<QObject*>(timer));
        }

        QObject::disconnect(timer, &QTimer::timeout, nullptr, nullptr);

        auto arguments = std::make_unique<std::tuple<decltype(args)...>>(std::forward<decltype(args)>(args)...);

        QObject::connect(timer, &QTimer::timeout, context, [shared_func, args = std::move(arguments)]() {
            std::apply(*shared_func, *args);
        });

        timer->start();
    };
}
#endif
