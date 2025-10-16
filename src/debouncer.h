/***************************************************************************
 *   copyright       : (C) 2025 by Kevin Exton <kevin.exton@pm.me>         *
 ***************************************************************************/
#pragma once
#ifndef Header_Debouncer
#define Header_Debouncer
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariant>

#include <cassert>
#include <memory>
#include <mutex>
namespace detail {
    /*!
     * \brief Manages the state and logic for debouncing a callable.
     * \tparam Func The type of the callable (e.g., function pointer, lambda)
     * that will be debounced.
     */
    template <typename Func>
    class Debouncer {
    public:
        /*! \brief The type of the callable. */
        using callable_type = std::remove_reference_t<Func>;
        /*! \brief The type of the connection. */
        using connection_type = QMetaObject::Connection;

        /*! \brief The context to debounce. */
        struct context_type
        {
            /*! \brief The object lifetime context. */
            QObject *context = nullptr;
            /*! \brief The callable. */
            std::unique_ptr<callable_type> func = nullptr;
            /*! \brief The debounce timer. */
            QPointer<QTimer> timer = nullptr;
            /*! \brief The object connection. */
            std::unique_ptr<connection_type> connection = std::make_unique<connection_type>();
        };

        /*! \brief The default constructor. */
        Debouncer() = default;

        /*!
         *  \brief Construct a debounced callable.
         *  \param func The callable.
         *  \param context The object lifetime context.
         *  \param durationMs The debounce timeout duration.
         */
        Debouncer(Func &&func, QObject *context, int durationMs)
            : m_context{
                .context = context,
                .func = std::make_unique<callable_type>(std::forward<Func>(func)),
                .timer = new QTimer(context)
            }
        {
            std::lock_guard lk{m_mutex};

            auto &timer = m_context.timer;
            timer->setInterval(durationMs);
            timer->setSingleShot(true);
        }

        /*! \brief Deleted copy constructor. */
        Debouncer(const Debouncer&) = delete;
        /*! \brief Deleted copy asssignment. */
        Debouncer& operator=(const Debouncer&) = delete;

        /*! \brief Move constructor. */
        Debouncer(Debouncer &&other) noexcept
            : Debouncer()
        {
            swap(*this, other);
        }

        /*! \brief Move assignment. */
        Debouncer& operator=(Debouncer &&other) noexcept
        {
            swap(*this, other);
            return *this;
        }

        /*! \brief swap function. */
        friend void swap(Debouncer &lhs, Debouncer &rhs) noexcept
        {
            using std::swap;
            if (&lhs == &rhs)
                return;

            std::scoped_lock lk(lhs.m_mutex, rhs.m_mutex);
            swap(lhs.m_context, rhs.m_context);
        }

        /*!
         *  \brief Invoke the callable.
         *  \param args The parameters to invoke the callable with.
         *  \tparam Args The argument types.
         */
        template <typename ...Args>
            requires std::invocable<callable_type, Args...>
        void operator()(Args &&...args) const {
            std::lock_guard lk(m_mutex);
            auto &[ctx, func, timer, conn] = m_context;
            assert(func && "The callable must be constructed before being called.");

            if (!timer)
                return;

            if (*conn)
                QObject::disconnect(*conn);

            *conn = QObject::connect(
                timer,
                &QTimer::timeout,
                ctx,
                [&, packed_args=std::forward_as_tuple(std::forward<Args>(args)...)]() {
                    std::apply(*func, std::move(packed_args));
                }
            );

            timer->start();
        }

        /*! \brief Destructor. */
        ~Debouncer() {
            std::lock_guard lk{m_mutex};

            if (auto &conn = m_context.connection; *conn)
                QObject::disconnect(*conn);
        }

    private:
        /*! \brief Debounced callable context. */
        context_type m_context;
        /*! \brief Mutex for thread-safety. */
        mutable std::mutex m_mutex;
    };
}

/*!
 * \brief Creates a debounced version of a slot or callable.
 * \tparam Func The type of the callable (e.g., function pointer, lambda).
 * \param[in] func The function/slot to debounce. It must be a callable that can be
 * wrapped in `std::function`.
 * \param[in] context The `QObject` that will own the internal `QTimer`. This is crucial
 * for the timer's lifetime management.
 * \param[in] durationMs The debounce delay in milliseconds.
 * \return A new lambda function that you can connect to a signal.
 */
template <typename Func>
decltype(auto) debounce(Func &&func, QObject *context, int durationMs = 300)
{
    using namespace detail;
    assert(context != nullptr && "Context object cannot be null.");
    assert(durationMs >= 0 && "Duration must be non-negative.");

    return [debouncer=Debouncer(std::forward<Func>(func), context, durationMs)](auto &&...args) {
        debouncer(std::forward<decltype(args)>(args)...);
    };
}
#endif
