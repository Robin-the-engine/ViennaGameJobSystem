#pragma once



#include <iostream>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <thread>
#include <future>
#include <vector>
#include <functional>
#include <condition_variable>
#include <queue>
#include <map>
#include <set>
#include <iterator>
#include <algorithm>
#include <assert.h>
#include <memory_resource>


namespace vgjs {

    class Coro_base;
    template<typename T> class Coro;
    class Coro_promise_base;

    template<typename>
    struct is_pmr_vector : std::false_type {};

    template<typename T>
    struct is_pmr_vector<std::pmr::vector<T>> : std::true_type {};

    //---------------------------------------------------------------------------------------------------

    /**
    * \brief Schedule a Coro into the job system.
    * Basic function for scheduling a coroutine Coro into the job system.
    * \param[in] coro A coroutine Coro, whose promise is a job that is scheduled into the job system
    */
    template<typename T>
    requires (std::is_base_of<Coro_base, T>::value)
    void schedule( T& coro ) noexcept {
        Job_base * parent = JobSystem::instance()->current_job();       //remember parent
        if (parent != nullptr) {
            parent->m_children++;                               //await the completion of all children      
        }
        coro.promise()->m_coro_parent = parent;
        JobSystem::instance()->schedule(coro.promise() );      //schedule the promise as job
    };

    /**
    * \brief Schedule a Coro into the job system.
    * Basic function for scheduling a coroutine Coro into the job system.
    * \param[in] coro A coroutine Coro, whose promise is a job that is scheduled into the job system
    */
    template<typename T>
    requires (std::is_base_of<Coro_base, T>::value)
    void schedule( T&& coro) noexcept {
        schedule(coro);
    };


    //---------------------------------------------------------------------------------------------------

    class Coro_promise_base;
    void deallocator(Coro_promise_base* job);

    /**
    * \brief Base class of coroutine Coro_promise, derived from Job so it can be scheduled.
    * 
    * The base promise class derives from Job so it can be scheduled on the job system!
    * The base does not depend on any type information that is stored with the promise.
    * It defines default behavior and the operators for allocating and deallocating memory
    * for the promise.
    */
    class Coro_promise_base : public Job_base {
    public:
        Job_base* m_coro_parent = nullptr;      //parent job that created this job

        Coro_promise_base() noexcept { m_continuation = this; };        //constructor

        /**
        * \brief Default behavior if an exception is not caught
        */
        void unhandled_exception() noexcept {   //in case of an exception terminate the program
            std::terminate();
        }

        /**
        * \brief When the coro is created it initially suspends
        */
        std::experimental::suspend_always initial_suspend() noexcept {  //always suspend at start when creating a coroutine Coro
            return {};
        }

        /**
        * \brief Use the given memory resource to create the promise object for a normal function.
        * 
        * Store the pointer to the memory resource right after the promise, so it can be used later
        * for deallocating the promise.
        * 
        * \param[in] sz Number of bytes to allocate
        * \param[in] std::allocator_arg_t Dummy parameter to indicate that the next parameter is the memory resource to use
        * \param[in] mr The memory resource to use when allocating the promise
        * \param[in] args the rest of the coro args
        * \returns a pointer to the newly allocated promise
        */
        template<typename... Args>
        void* operator new(std::size_t sz, std::allocator_arg_t, std::pmr::memory_resource* mr, Args&&... args) noexcept {
            //std::cout << "Coro new1 " << sz << "\n";
            auto allocatorOffset = (sz + alignof(std::pmr::memory_resource*) - 1) & ~(alignof(std::pmr::memory_resource*) - 1);
            char* ptr = (char*)mr->allocate(allocatorOffset + sizeof(mr));
            if (ptr == nullptr) {
                std::terminate();
            }
            *reinterpret_cast<std::pmr::memory_resource**>(ptr + allocatorOffset) = mr;
            return ptr;
        }

        /**
        * \brief Use the given memory resource to create the promise object for a member function.
        *
        * Store the pointer to the memory resource right after the promise, so it can be used later
        * for deallocating the promise.
        *
        * \param[in] sz Number of bytes to allocate
        * \param[in] Class The class that defines this member function
        * \param[in] std::allocator_arg_t Dummy parameter to indicate that the next parameter is the memory resource to use
        * \param[in] mr The memory resource to use when allocating the promise
        * \param[in] args the rest of the coro args
        * \returns a pointer to the newly allocated promise
        */
        template<typename Class, typename... Args>
        void* operator new(std::size_t sz, Class, std::allocator_arg_t, std::pmr::memory_resource* mr, Args&&... args) noexcept {
            return operator new(sz, std::allocator_arg, mr, args...);
        }

        /**
        * \brief Create a promise object for a class member function using the system standard allocator.
        * \param[in] sz Number of bytes to allocate
        * \param[in] Class The class that defines this member function
        * \param[in] args the rest of the coro args
        * \returns a pointer to the newly allocated promise
        */
        template<typename Class, typename... Args>
        void* operator new(std::size_t sz, Class, Args&&... args) noexcept {
            return operator new(sz, std::allocator_arg, std::pmr::new_delete_resource(), args...);
        }

        /**
        * \brief Create a promise object using the system standard allocator.
        * \param[in] sz Number of bytes to allocate
        * \param[in] args the rest of the coro args
        * \returns a pointer to the newly allocated promise
        */
        template<typename... Args>
        void* operator new(std::size_t sz, Args&&... args) noexcept {
            //std::cout << "Coro new2 " << sz << "\n";
            return operator new(sz, std::allocator_arg, std::pmr::new_delete_resource(), args...);
        }

        /**
        * \brief Use the pointer after the promise as deallocator
        * \param[in] ptr Pointer to the memory to deallocate
        * \param[in] sz Number of bytes to deallocate
        */
        void operator delete(void* ptr, std::size_t sz) noexcept {
            //std::cout << "Coro delete " << sz << "\n";
            auto allocatorOffset = (sz + alignof(std::pmr::memory_resource*) - 1) & ~(alignof(std::pmr::memory_resource*) - 1);
            auto allocator = (std::pmr::memory_resource**)((char*)(ptr)+allocatorOffset);
            (*allocator)->deallocate(ptr, allocatorOffset + sizeof(std::pmr::memory_resource*));
        }
    };

    template<typename T>
    class Coro_promise;

    //---------------------------------------------------------------------------------------------------

    /**
    * \brief Base class of coroutine Coro. Independent of promise return type.
    */
    class Coro_base {
    public:
        Coro_base() noexcept {};                                              //constructor
        virtual bool resume() noexcept { return true; };                     //resume the Coro
        virtual Coro_promise_base* promise() noexcept { return nullptr; };   //get the promise to use it as Job 
    };

    //---------------------------------------------------------------------------------------------------

    /**
    * \brief Base class of awaiter, contains default behavior.
    */
    struct awaiter_base {
        bool await_ready() noexcept {   //default: go on with suspension
            return false;
        }

        void await_resume() noexcept {} //default: no return value
    };

    /**
    * \brief Awaiter for awaiting a tuple of vector of Coros of type Coro<T> or std::function<void(void)>
    *
    * The tuple can contain vectors with different types.
    * The caller will then await the completion of the Coros. Afterwards,
    * the return values can be retrieved by calling get().
    */
    template<typename... Ts>
    struct awaitable_tuple {

        struct awaiter : awaiter_base {
            Coro_promise_base* m_promise;
            std::tuple<std::pmr::vector<Ts>...>& m_tuple;                //vector with all children to start

            bool await_ready() noexcept {                                 //suspend only if there are no Coros
                auto f = [&, this]<std::size_t... Idx>(std::index_sequence<Idx...>) {
                    std::size_t num = 0;
                    std::initializer_list<int>{ ( num += std::get<Idx>(m_tuple).size(), 0) ...}; //called for every tuple element
                    return (num == 0);
                };
                return f(std::make_index_sequence<sizeof...(Ts)>{}); //call f and create an integer list going from 0 to sizeof(Ts)-1
            }

            void await_suspend(std::experimental::coroutine_handle<> continuation) noexcept {
                auto f = [&, this]<std::size_t... Idx>(std::index_sequence<Idx...>) {
                    std::initializer_list<int>{ ( schedule( std::get<Idx>(m_tuple) ) , 0) ...}; //called for every tuple element
                };
                f(std::make_index_sequence<sizeof...(Ts)>{}); //call f and create an integer list going from 0 to sizeof(Ts)-1
            }

            awaiter(Coro_promise_base* promise, std::tuple<std::pmr::vector<Ts>...>& children, int32_t thread_index = -1) noexcept
                : m_promise(promise), m_tuple(children) {};
        };

        Coro_promise_base* m_promise;
        std::tuple<std::pmr::vector<Ts>...>& m_tuple;              //vector with all children to start

        awaitable_tuple(Coro_promise_base* promise, std::tuple<std::pmr::vector<Ts>...>& children ) noexcept 
            : m_promise(promise), m_tuple(children) {};

        awaiter operator co_await() noexcept { return { m_promise, m_tuple }; };
    };


    /**
    * \brief Awaiter for awaiting a Coro of type Coro<T> or std::function<void(void)>, or std::pmr::vector thereof
    *
    * The caller will await the completion of the Coro(s). Afterwards,
    * the return values can be retrieved by calling get() for Coro<t>
    */
    template<typename T>
    struct awaitable_coro {

        struct awaiter : awaiter_base {
            Coro_promise_base* m_promise;
            T& m_child;      //child Coro

            bool await_ready() noexcept {             //suspend only there are no Coros
                if constexpr (is_pmr_vector<T>::value) {
                    return m_child.empty();
                }
                return false;
            }

            void await_suspend(std::experimental::coroutine_handle<> continuation) noexcept {
                schedule( std::forward<T>(m_child) );  //schedule the promise or function as job by calling the correct version
            }

            awaiter(Coro_promise_base* promise, T& child) noexcept : m_promise(promise), m_child(child) {};
        };

        Coro_promise_base* m_promise;
        T& m_child;              //child Coro

        awaitable_coro(Coro_promise_base* promise, T& child ) noexcept : m_promise(promise), m_child(child) {};

        awaiter operator co_await() noexcept { return { m_promise, m_child }; };
    };


    /**
    * \brief Awaiter for changing the thread that the job is run on
    */
    struct awaitable_resume_on {
        struct awaiter : awaiter_base {
            Coro_promise_base*  m_promise;
            int32_t             m_thread_index;

            bool await_ready() noexcept {   //default: go on with suspension
                return m_thread_index == JobSystem::instance()->thread_index();
            }

            void await_suspend(std::experimental::coroutine_handle<> continuation) noexcept {
                m_promise->m_thread_index = m_thread_index;
            }

            awaiter(Coro_promise_base* promise, int32_t thread_index) noexcept : m_promise(promise), m_thread_index(thread_index) {};
        };

        Coro_promise_base*  m_promise;
        int32_t             m_thread_index;

        awaitable_resume_on(Coro_promise_base* promise, int32_t thread_index) noexcept : m_promise(promise), m_thread_index(thread_index) {};

        awaiter operator co_await() noexcept { return { m_promise, m_thread_index }; };
    };

    //---------------------------------------------------------------------------------------------------

    /**
    * \brief Promise of the Coro. Depends on the return type.
    * 
    * The Coro promise can hold values that are produced by the Coro. They can be
    * retrieved later by calling get() on the Coro (which calls get() on the promise)
    */
    template<typename T>
    class Coro_promise : public Coro_promise_base {
        template<typename U> struct final_awaiter;
        template<typename U> friend struct final_awaiter;
        template<typename U> friend class Coro;

    private:
        T m_value{};                    //the value that should be returned

    public:

        Coro_promise() noexcept : Coro_promise_base{}, m_value{} {};

        /**
        * \brief Get Coro coroutine future from promise.
        * \returns the Coro future from the promise.
        */
        Coro<T> get_return_object() noexcept {
            return Coro<T>{ std::experimental::coroutine_handle<Coro_promise<T>>::from_promise(*this) };
        }

        /**
        * \brief Resume the Coro at its suspension point.
        */
        bool resume() noexcept {
            auto coro = std::experimental::coroutine_handle<Coro_promise<T>>::from_promise(*this);
            if (coro) {
                if (!coro.done()) {
                    m_children = 1;
                    coro.resume();              //coro could destroy itself here!!
                }
            }
            return true;
        };

        /**
        * \brief Store the value returned by co_return.
        * \param[in] t The value that was returned.
        */
        void return_value(T t) noexcept {   //is called by co_return <VAL>, saves <VAL> in m_value
            m_value = t;
        }

        /**
        * \brief Return the store value.
        * \returns the stored value.
        */
        T get() noexcept {      //return the stored m_value to the caller
            return m_value;
        }

        /**
        * \brief Deallocate the promise because the queue is shutting down
        */
        bool deallocate() noexcept {    //called when the job system is destroyed
            auto coro = std::experimental::coroutine_handle<Coro_promise<T>>::from_promise(*this);

            if (coro) {
                coro.destroy();
            }

            return false;
        };

        /**
        * \brief Return an awaitable from a tuple of vectors.
        * \returns the correct awaitable.
        */
        template<typename... Ts>    //called by co_await for std::pmr::vector<Ts>& Coros or functions
        awaitable_tuple<Ts...> await_transform( std::tuple<std::pmr::vector<Ts>...>& coros) noexcept {
            return { this, coros };
        }

        /**
        * \brief Return an awaitable from basic types like functions, Coros, or vectors thereof
        * \returns the correct awaitable.
        */
        template<typename T>        //called by co_await for Coros or functions, or std::pmr::vector thereof
        awaitable_coro<T> await_transform(T& coro) noexcept {
            return { this, coro };
        }

        /**
        * \brief Return an awaitable for an integer number.
        * This is used when the coro should change the current thread.
        * \returns the correct awaitable.
        */
        awaitable_resume_on await_transform(int thread_index) noexcept { //called by co_await for INT, for changing the thread
            return { this, (int32_t)thread_index };
        }

        /**
        * \brief When a coroutine reaches its end, it may suspend a last time using such a final awaiter
        *
        * Suspending as last act prevents the promise to be destroyed. This way the caller
        * can retrieve the stored value by calling get(). Also we want to resume the parent
        * if all children have finished their Coros.
        * If the parent was a Job, then if the Coro<T> is still alive, the coro will suspend,
        * and the Coro<T> must destroy the promise in its destructor. If the Coro<T> has destructed,
        * then the coro must destroy the promise itself by resuming the final awaiter.
        */
        template<typename U>
        struct final_awaiter : public awaiter_base {
            final_awaiter() noexcept {}

            void await_suspend(std::experimental::coroutine_handle<Coro_promise<U>> h) noexcept { //called after suspending
                auto& promise = h.promise();
                promise.m_parent = promise.m_coro_parent;   //enable parent notification
                promise.m_continuation = nullptr;           //disable the coro to continue
                return;  //if false then the coro frame is destroyed, but we might want to get the result first
            }
        };

        final_awaiter<T> final_suspend() noexcept { //create the final awaiter at the final suspension point
            return {};
        }
    };

    //---------------------------------------------------------------------------------------------------

    /**
    * \brief The main Coro class. Can be constructed to return any value type
    *
    * The Coro is an accessor class much like a std::future that is used to
    * access the promised value once it is awailable.
    * It also holds a handle to the Coro promise.
    */
    template<typename T>
    class Coro : public Coro_base {
    public:

        using promise_type = Coro_promise<T>;

    private:
        std::experimental::coroutine_handle<promise_type> m_coro;   //handle to Coro promise

    public:
        Coro(Coro<T>&& t) noexcept : m_coro(std::exchange(t.m_coro, {})) {}
        void operator= (Coro<T>&& t) { std::swap( m_coro, t.m_coro); }

        /**
        * \brief Destructor of the Coro promise. Might deallocate the promise,
        * if the promise has finished. This is done only if the parent is a normal function,
        * i.e. a Job.
        */
        ~Coro() noexcept {
            if (m_coro ) { //do not ask for done()!

                if ( current_job() != nullptr) { //m_coro.promise().m_coro_parent != nullptr) {         //if the parent is a coro then destroy the coro, 
                    if (!current_job()->is_job() ) { //!m_coro.promise().m_coro_parent->is_job()) {     //because they are in sync 
                        m_coro.destroy();                           //if you do not want this then move Coro
                    }
                    else {  //if the parent is a job+function, then the function often returns before the child finishes
                        int count = m_coro.promise().m_count.fetch_sub(1);
                        if (count == 1) {       //if the coro is done, then destroy it
                            m_coro.destroy();
                        }
                    }
                }

            }
        }

        /**
        * \brief Retrieve the promised value
        * \returns the value that is stored by this Coro future.
        */
        T get() noexcept {                  //
            return m_coro.promise().get();
        }

        /**
        * \brief Retrieve a pointer to the promise.
        * \returns a pointer to the promise.
        */
        Coro_promise_base* promise() noexcept { //get a pointer to the promise (can be used as Job)
            return &m_coro.promise();
        }

        /**
        * \brief Function operator so you can pass on parameters to the Coro.
        * 
        * \param[in] thread_index The thread that should execute this coro
        * \param[in] type The type of the coro.
        * \param[in] id A unique ID of the call.
        * \returns a reference to this Coro so that it can be used with co_await.
        */
        Coro<T>&& operator() (int32_t thread_index = -1, int32_t type = -1, int32_t id = -1 ) {
            m_coro.promise().m_thread_index = thread_index;
            m_coro.promise().m_type = type;
            m_coro.promise().m_id = id;
            return std::move(*this);
        } 

        /**
        * \brief Resume the coro at its suspension point
        */
        bool resume() noexcept {    //resume the Coro by calling resume() on the handle
            if (m_coro) {
                if (!m_coro.done()) {
                    m_coro.promise().resume();
                }
            }
            return true;
        };

        explicit Coro(std::experimental::coroutine_handle<promise_type> h) noexcept : m_coro(h) {}
    };

}

