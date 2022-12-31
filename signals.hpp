
/**
 * based on https://github.com/walker-zheng/code/blob/master/work/simplesignal2.cpp
 * MIT license.
 * */

#ifndef __std_signals_hpp
#define __std_signals_hpp

#include <functional>
#include <vector>
#include <memory>

#ifdef STD_SIGNALS_INJECT_INTO_STD_NAMESPACE
#   define __STD_NAMESPACE_BEG namespace std {
#   define __STD_NAMESPACE_END }
#else
#   define __STD_NAMESPACE_BEG
#   define __STD_NAMESPACE_END
#endif // STD_SIGNALS_INJECT_INTO_STD_NAMESPACE

__STD_NAMESPACE_BEG

namespace signals {

// forwards
struct connection;
struct scoped_connection;

namespace details {

/*************************************************************************************************/

/** Undefined template class for invoking slots differently
    depending on their return type. */
template<typename>
struct ConnectionInvoker;

/** ConnectionInvoker specialisation for value-returning slots. */
template<typename Result, typename... Args>
struct ConnectionInvoker<Result(Args...)> {
    /** Call the slot, handing its return value over to the aggregation.
        @returns `true` iff the aggregation determines that emission
        should continue. */
    template <typename Aggregation, typename Controller>
    static bool invoke(
        const std::function<Result(Args...)> & f
        ,Args... args
        ,Aggregation & aggregation
        ,Controller & controller)
    {
        Result result = f(args...);
        bool ok = controller(result);
        aggregation.aggregate(std::move(result));

        return ok;
    }
};

/** ConnectionInvoker specialisation for void slots. */
template<typename... Args>
struct ConnectionInvoker<void(Args...)> {
    /** Call the slot.
        @returns `true` iff the aggregation determines that emission
        should continue. */
    template <typename Aggregation, typename Controller>
    static bool invoke(
        const std::function<void(Args...)> & f
        ,Args... args
        ,Aggregation & aggregation
        ,Controller & controller)
    {
        f(args...);
        bool ok = controller();
        aggregation.aggregate();

        return ok;
    }
};

struct signal_base {
    using connection_handler = std::pair<details::signal_base *, void *>;

    signal_base()
        :m_head{}
        ,m_recursionDepth{}
        ,m_deactivations{}
    {}
    virtual ~signal_base() = default;

    bool disconnect(const connection_handler &con) {
        if ( con.first != this ) {
            return false;
        }

        return disconnect(con.second);
    }
    bool disconnect(void* id);

    bool valid(const connection &con) const { return connected(con); }
    bool valid(const scoped_connection &con) const { return connected(con); }
    bool valid(const connection_handler &con) const { return connected(con); }

    bool connected(const connection &con) const;
    bool connected(const scoped_connection &con) const;
    bool connected(const connection_handler &con) const {
        if ( con.first != this ) {
            return false;
        }

        return connected(con.second);
    }
    bool connected(void* id) const;

    /** Insert entry point into connection ring */
    std::unique_ptr<void, std::function<void(void*)>> m_head;
    /** Guard against iteration invalidation by slots that alter the Signal */
    mutable std::size_t m_recursionDepth;
    /** Have any connections been deactivated (but not deleted)? */
    mutable bool m_deactivations;
};

struct node_base {
    node_base()
        :m_next{this}
        ,m_prev{this}
    {}
    virtual ~node_base() = default;

    /** Insert `node` immediately before this node in the ring.
    @returns the inserted node */
    node_base* insert(node_base *node) {
        if ( node ) {
            node->m_prev = m_prev;
            node->m_next = this;
            m_prev->m_next = node;
            m_prev = node;
        }

        return node;
    }

    /** Extracts this node from the list.
        @returns this node */
    node_base* extract() {
        m_next->m_prev = m_prev;
        m_prev->m_next = m_next;
        m_prev = m_next = nullptr;

        return this;
    }

    /** @returns the node which follows this one in the ring. If this
        is the only node in the ring, then returns this node. */
    node_base* next() { return m_next; }

    virtual void deactivate() = 0;

private:
    /** The next node in the ring. */
    node_base *m_next;
    /** The previous node in the ring. */
    node_base *m_prev;
};

bool signal_base::disconnect(void* id) {
    if ( m_head ) {
        // Scan the list for the matching connection
        node_base *node = static_cast<node_base*>(m_head.get())->next();
        while ( node != m_head.get() && node != id ) {
            node = node->next();
        }

        if ( node == id ) {
            // Delete or deactivate the connection
            if ( m_recursionDepth == 0 ) {
                delete node->extract();
            } else {
                node->deactivate();
                m_deactivations = true;
            }
            return true;
        }
    }

    return false;
}

bool signal_base::connected(void* id) const {
    if ( m_head ) {
        // Scan the list for the matching connection
        node_base *node = static_cast<node_base*>(m_head.get())->next();
        for ( ; node != m_head.get(); node = node->next() ) {
            if ( node == id ) {
                return true;
            }
        }
    }

    return false;
}

/** Node implements a doubly-linked ring, with each node containing a
    function of type `Func`. */
template<typename Func>
struct node: node_base {
    explicit node(Func f)
        :node_base{}
        ,m_function{std::move(f)}
    {}
    virtual ~node() = default;

    /** @returns this node's function */
    const Func& function() const { return m_function; }

    /** Deactivates this node (by nullifying its function) thereby
    marking this node as suitable for extraction from the list when
    convenient.
    @returns this node */
    void deactivate() { m_function = nullptr; }

private:
    /** The node's function */
    Func m_function;
};

/** Exception safe counter incrementer, i.e. ensures counter is
    decremented if exception is thrown while this object is in
    scope. */
template <typename T>
struct ScopedIncrement {
    /** Constructor
        @param[in,out] counter The counter to be incremented */
    explicit ScopedIncrement(T & counter)
        :m_counter(counter)
    { ++m_counter; }

    /** Destructor. Decrements the counter */
    ~ScopedIncrement()
    { --m_counter; }

private:
    T &m_counter;
};

}  // namespace details

/*************************************************************************************************/

/** Continue signal emission regardless of the slot return value */
template<typename Result>
struct condition_all {
    bool operator()(const Result &) { return true; }
};

/** Continue signal emission -- specialisation for void slots. */
template<>
struct condition_all<void> {
    bool operator()() const { return true; }
};

/** Continue signal emission if the slot returns a result that
    (when converted to a boolean) equals `T`. */
template<typename Result, bool T = true>
struct condition_while {
    bool operator()(const Result & result) const { return result == T; }
};

/*************************************************************************************************/

/** Aggregation that ignores slot return type and value, returning
    void. This is the default aggregation used by a signal's `emit()`
    function. */
template<typename Result>
struct aggregation_void {
    /** Discards slot return value */
    void aggregate(Result) {}
    /** @returns nothing, irrespective of the slot return type */
    void get() {}
};

/** Specialisation of Void for void slots. */
template<>
struct aggregation_void<void> {
    /** Does nothing */
    void aggregate() {}
    /** @returns nothing */
    void get() {}
};

/** Aggregation that returns the value returned by the last slot
    called in a signal emission. If a signal has no connected slots,
    the return value is a value-initialized `Result`. */
template<typename Result>
struct aggregation_last {
    /** Keeps hold of only the latest slot return value. */
    void aggregate(Result r) { m_latest = std::move(r); }
    /** @returns the return value of the last slot called from the
        signal emission. */
    Result get() { return std::move(m_latest); }

private:
    Result m_latest{};  // Ensure value-initialisation
};

/** Aggregation that collates the values returned by each slot in a
    container. This template class is suitable for most standard
    library containers. By default, a `std::vector` is used.  If a
    signal has no connected slots, the return value is an empty
    container. */
template<
    typename Result
    ,template<typename, typename> class Container = std::vector
    ,typename Allocator = std::allocator<Result>
    ,typename Cont = Container<Result, Allocator>

>
struct aggregation_collation {
    /** Append the slot return value within the container. */
    void aggregate(Result r) { m_result.insert(m_result.end(), std::move(r)); }
    /** @returns the container of collated slot return values. */
    Cont get() { return std::move(m_result); }

private:
    Cont m_result;
};

/** Aggregation that counts the number of slots called. */
template<typename Result>
struct aggregation_counter {
    /** Increment counter and discard slots return value */
    void aggregate(Result) { ++m_result; }
    /** @returns the number of slots called. */
    std::size_t get() const { return m_result; }

private:
    std::size_t m_result{};
};

/** Aggregation that counts the number of slots called,
    specialised for void slots. */
template<>
struct aggregation_counter<void> {
    /** Increment counter */
    void aggregate() { ++m_result; }
    /** @returns the number of slots called. */
    std::size_t get() const { return m_result; }

private:
    std::size_t m_result{};
};

/*************************************************************************************************/

using connection_handler = details::signal_base::connection_handler;

struct connection {
    friend details::signal_base;

    connection(connection_handler con)
        :m_con{std::move(con)}
    {}

    bool connected() const { return m_con.first->connected(m_con.second); }

    bool disconnect() { return m_con.first->disconnect(m_con.second); }

private:
    connection_handler m_con;
};

struct scoped_connection {
    friend details::signal_base;

    scoped_connection(connection_handler con)
        :m_con{std::move(con)}
    {}

    ~scoped_connection() { disconnect(); }

    bool connected() const { return m_con.first->connected(m_con.second); }

    bool disconnect() { return m_con.first->disconnect(m_con.second); }

private:
    connection_handler m_con;
};

namespace details {

bool signal_base::connected(const connection &con) const { return connected(con.m_con); }

bool signal_base::connected(const scoped_connection &con) const { return connected(con.m_con); }

} // ns details

/*************************************************************************************************/

/** Undefined template class used for specialization. */
template<typename Signature>
struct signal;

/** Signal is a template class providing an implementation of the
    infrastructure for the Observer Pattern.

    The Observer Pattern's subject contains or references one or more
    _signal_ objects. A signal is emitted when some event occurs.

    Each observer connects a _slot_ to the subject's appropriate
    signal. This is conceptually identical to Qt's celebrated 'signals
    and slots' mechanism.

    A slot is an arbitrary function, lambda expression, bind
    expression, other function object, or a pointer to a member
    function. All of a signal's connected slots must have identical
    signatures. The Signal template class is instantiated with the
    function signature shared by all of its slots.

    While the term _slot_ is used herein, alternative, commonly used
    and equally good terms include _callback_, _callback function_,
    _notification callback_, _receiver_ as well as _observer_.

    A signal is connected to a slot using `connect()`. These
    connections form a many-to-many relationship between signals and
    slots. A connection's existence implies that the emission of the
    signal will be transmitted to the slot.

    The Signal class is responsible for managing its connections to
    slots. Signal-slot connections can be removed with `disconnect()`.

    The signal is emitted to its connected slots using the `emit()`. */
template<typename Result, typename... Args>
struct signal<Result(Args...)>: details::signal_base {
    using slot_type = std::function<Result(Args...)>;
    using node_type = details::node<slot_type>;

    /** Constructor. Constructs a Signal with no connections to
        slots. */
    signal()
        :details::signal_base{}
    {}

    /** Constructor. Constructs a Signal with a single connection to the
        given slot */
    explicit signal(slot_type f)
        :signal()
    { connect(std::move(f)); }

    signal(const signal &) = delete;

    signal(signal &&) noexcept = default;

    signal & operator=(const signal &) = delete;

    virtual ~signal() {
        if ( m_head ) {
            while ( static_cast<node_type*>(m_head.get())->next() != m_head.get() ) {
                disconnect(static_cast<node_type*>(m_head.get())->next());
            }
        }
    }

    /** Add a connection from this signal to the given slot. The slot
        can be a function, lambda expression, bind expression or another
        function object. Member functions can be connected as slots
        using alternative overloads of `connect()`.
        @returns an identifier for the newly added signal-slot connection. */
    connection_handler connect(slot_type f) {
        if ( !m_head ) {
            m_head = std::unique_ptr<void, std::function<void(void*)>>(
                 new node_type{nullptr}
                ,[](void *p){ delete static_cast<node_type*>(p); }
            );
        }

        return {
             this
            ,static_cast<node_type*>(m_head.get())->insert(new node_type{std::move(f)})
        };
    }

    /** Add a connection from this signal to a member function of the
        referenced object.
        @returns an identifier for the newly added signal-slot connection. */
    template<typename C>
    connection_handler connect(C & obj, Result (C::*mf)(Args...)) {
        return connect([&obj, mf](Args... args) { return (obj.*mf)(args...); });
    }

    /** Add a connection from this signal to a member function of the
        referenced const object.
        @returns an identifier for the newly added signal-slot connection. */
    template<typename C>
    connection_handler connect(const C & obj, Result (C::*mf)(Args...)) {
        return connect([&obj, mf](Args... args) { return (obj.*mf)(args...); });
    }

    /** Add a connection from this signal to a member function of the
        pointed-to object.
        @returns an identifier for the newly added signal-slot connection. */
    template<typename C>
    connection_handler connect(C * obj, Result (C::*mf)(Args...)) {
        return obj ? connect(*obj, mf) : connection_handler{};
    }

    /** Add a connection from this signal to a member function of the
        pointed-to const object.
        @returns an identifier for the newly added signal-slot connection. */
    template<typename C>
    connection_handler connect(const C *obj, Result (C::*mf)(Args...)) {
        return obj ? connect(*obj, mf) : connection_handler{};
    }

    /** Emit a signal to all its connected slots, optionally collating
        the values they return.

        ##### Slot calling order #####

        Slots are called in the order in which they are connected to the
        signal, using the `connect()` function.

        ##### Signal modification and recursion during emission #####

        The implementation is safe against recursion, so slots may be
        connected to and disconnected from the signal during signal
        emission and recursive `emit()` calls are also safe.

        Slots connected to this signal during an emission will be called
        in that emission.

        Slots disconnected from this signal during an emission will not
        be called in that emission, unless they were called prior to
        their disconnection.

        The result of emission depends on the aggregation used to
        instantiate the `emit()` template function. By default, nothing
        is returned, i.e. `emit()` is a void function.

        ##### Aggregation #####

        An Aggregation is a class that collates the slot return values
        into a single return value. Examples are:

          - Ignore the return value (if any) from the slots and return
            void [Default].
          - Return the result of the last slot called during emission.
          - Return the results of all the calls to each of a signal's
            connected slots in a collection, such as a vector.
          - Return the number of slots which returned true/non-null
            values.

        An implementation of an Aggregation class must implement the
        following member functions:

        - `void aggregate(TypeA) {...}` -- where `TypeA` is the slot
          return type, or is implicitly convertable from it.
        - `TypeB get() {...}` -- where `TypeB` is at the
          implementer's discretion, though it is often the same as
          `TypeA`.

        An aggregation's `aggregate()` function accepts the value
        returned from each slot and either does something with it or
        ignores it.

        An aggregation's `get()` function returns the aggregated result.

        Some example Aggregation implementations are available in the
        `aggregation` namespace.

        ##### Controller #####

        A controller is a callable object that determines if or when to
        halt running emissions. Typically, though not necessarily, this
        decision is based on the slots' return value.

        The controller function should have the form:

        - `bool func(const Result &)` or
        - `bool func()`

        For value-returning and void slots respectively, where `Result`
        is the return type of the slots (or is implicitly convertable
        from it).

        @returns the output of the aggregation's `get()` function.
    */
    template<
         typename Aggregation = typename std::conditional
         <   std::is_same<typename std::remove_cv<Result>::type, void>::value
            ,aggregation_void<Result>
            ,aggregation_last<Result>
         >::type
        ,typename Controller = condition_all<Result>
    >
    auto emit(
         Args... args
        ,Aggregation aggregation = Aggregation()
        ,Controller controller = Controller()) const
        -> decltype(aggregation.get())
    {
        using Invoker = details::ConnectionInvoker<Result(Args...)>;
        using Increment = details::ScopedIncrement<decltype(m_recursionDepth)>;

        if ( m_head ) {
            node_type *node = static_cast<node_type*>(static_cast<details::node_base*>(m_head.get())->next());
            for ( bool ok = true; node != m_head.get() && ok; ) {
                /* Increment the recursion counter to ensure that slots cannot
                   recursively delete connections from the ring (thereby
                   potentially invalidating this iteration), rather they only
                   deactivate them. Adding connections is okay. */
                Increment scopedIncrement{m_recursionDepth};
                if ( node->function() != nullptr ) {
                    ok = Invoker::invoke(node->function(), args..., aggregation, controller);
                }

                node = static_cast<node_type*>(node->next());
            }

            if ( m_recursionDepth == 0 && m_deactivations ) {
                /* A slot has recursively deactivated one or more connections
                   from this signal. Delete these deactivated connections
                   now. */
                node = static_cast<node_type*>(static_cast<details::node_base*>(m_head.get())->next());
                while ( node != m_head.get() ) {
                    node_type *next = static_cast<node_type*>(node->next());
                    if ( node->function() == nullptr ) {
                        delete static_cast<node_type*>(node->extract());
                    }
                    node = next;
                }
                m_deactivations = false;
            }
        }
        return aggregation.get();
    }

    template<
         typename Aggregation = typename std::conditional
         <   std::is_same<typename std::remove_cv<Result>::type, void>::value
            ,aggregation_void<Result>
            ,aggregation_last<Result>
         >::type
        ,typename Controller = condition_all<Result>
    >
    auto operator()(
         Args... args
        ,Aggregation aggregation = Aggregation()
        ,Controller controller = Controller()) const
        -> decltype(emit(args..., aggregation, controller))
    {
        return emit(args..., aggregation, controller);
    }
};

/*************************************************************************************************/

}  // namespace signals

__STD_NAMESPACE_END

#endif // __std_signals_hpp
