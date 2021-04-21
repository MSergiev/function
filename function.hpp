#pragma once

#include <optional>
#include <functional>
#include <memory>

namespace hpp
{
    template<typename T>
    using optional = std::optional<T>;

    static constexpr auto& no_value = std::nullopt;

    template<typename T>
    class sentinel_t
    {
        std::weak_ptr<T> ptr_;
        
    public:

        sentinel_t() = default;

        sentinel_t( const std::shared_ptr<T>& ptr )
            : sentinel_t( std::weak_ptr<T>(ptr) ) 
        {}

        sentinel_t( const std::weak_ptr<T>& ptr ) 
            : ptr_( ptr )
        {}

        auto expired() const -> bool
        {
            return ptr_.expired();
        }
    };
    
    using sentinel_opt_t = optional<sentinel_t>;

    // To be instantiated or inherited by function user classes for automatic sentinel generation and expiration
    class lifetime_sentinel
    {
        struct _signal_guard_t {};
        using _signal_guard_ptr = std::shared_ptr<_signal_guard_t>;
        mutable _signal_guard_ptr _guard_ptr_ = std::make_shared<_signal_guard_t>();
        mutable sentinel_t _sentinel_ { _guard_ptr_ };

    public:

        auto get_sentinel() const -> sentinel_t&
        {
            return _sentinel_;
        }
    };

    template<typename ReturnType>
    auto has_value( const ReturnType& val ) -> bool
    {
        return ( val != no_value );
    }

// Enablers

    template<typename T>
    constexpr auto is_sentinel()
    {
        return std::is_same<T, sentinel_t>::value ||
               std::is_same<T, sentinel_opt_t>::value ||
               std::is_same<T, std::nullopt_t>::value;
    }

    template<typename Source, typename Target>
    constexpr auto is_different_function()
    {
        return !std::is_same<Source, typename std::decay<Target>::type>::value;
    }

    template<typename Source, typename Target>
    using _function_enabler = typename std::enable_if<is_different_function<Source, Target>()>::type;

    template<typename Source, typename Target, typename Sentinel>
    using _sentinel_function_enabler = typename std::enable_if<is_different_function<Source, Target>() && is_sentinel<Sentinel>()>::type;

    // Check if type is void
    template<typename T>
    using is_void = std::is_same<T, void>;

    // Void return enabler
    template<typename ReturnType>
    using returns_void = typename std::enable_if<is_void<ReturnType>::value>::type;

    // Value return enabler
    template<typename ReturnType>
    using returns_value = typename std::enable_if<!is_void<ReturnType>::value>::type;

// Exceptions

    // Can be thrown from the function to force a return value of nullopt for error handling
    struct function_exception : public std::exception
    {
        explicit function_exception( bool passthrough = false )
            : error_msg_( "Unspecified error" ),
              passtrhrough_( passthrough )
        {}

        explicit function_exception( const char* message,
                                     bool passthrough = false )
            : error_msg_( message ),
              passtrhrough_( passthrough )
        {}

        explicit function_exception( std::string message,
                                     bool passthrough = false )
            : error_msg_( std::move(message) ),
              passtrhrough_( passthrough )
        {}

        auto what() const noexcept -> const char* override
        {
            return error_msg_.c_str();
        }

        auto is_passthrough() const noexcept -> bool
        {
            return passtrhrough_;
        }

    protected:

        // Exception description
        const std::string error_msg_;

        // If set, the exception will be rethrown outside the function for external handling
        const bool passtrhrough_;
    };

// Function definition

    // Basic function methods
    template<typename FunctionSignature>
    struct _function_container
    {
        using func_t = std::function<FunctionSignature>;

    // Utilities

        void disconnect() const
        {
            slot_ = {};
        }

        auto empty() const -> bool
        {
            return !slot_.func;
        }

        auto expired() const -> bool
        {
            if( slot_.sentinel == no_value )
            {
                return false;
            }

            return slot_.sentinel.value().expired();
        }

        auto valid() const -> bool
        {
            return !expired() && !empty();
        }

        operator bool() const
        {
            return valid();
        }

    protected:

        template<typename... FunctionArgs>
        void connect_impl( const sentinel_opt_t& sentinel, FunctionArgs&&... args )
        {
            slot_.func = { std::forward<FunctionArgs>(args)... };
            slot_.sentinel = sentinel;
        }

        mutable struct
        {
            func_t func {};
            sentinel_opt_t sentinel {};
        } slot_;
    };

    // Function call method specializations
    template<typename... Args>
    struct _void_function_base
        : _function_container<void(Args...)>
    {
        template<typename... FunctionArgs>
        void operator()( FunctionArgs&&... args ) const
        {
            try
            {
                if( this->valid() )
                {
                    this->slot_.func( std::forward<FunctionArgs>(args)... );
                }
            }
            catch( const function_exception& e )
            {
                if( e.is_passthrough() )
                {
                    throw;
                }
            }
        }

        void operator()( Args&&... args ) const
        {
            try
            {
                if( this->valid() )
                {
                    this->slot_.func( std::move(args)... );
                }
            }
            catch( const function_exception& e )
            {
                if( e.is_passthrough() )
                {
                    throw;
                }
            }
        }
    };

    template<typename ReturnType, typename... Args>
    struct _value_function_base
        : _function_container<ReturnType(Args...)>
    {
        static_assert( !std::is_reference<ReturnType>::value, "Function return type cannot be a reference" );
        using return_t = optional<ReturnType>;

        template<typename... FunctionArgs>
        auto operator()( FunctionArgs&&... args ) const -> return_t
        {
            try
            {
                if( this->valid() )
                {
                    return return_t{ this->slot_.func(std::forward<FunctionArgs>(args)...) };
                }
            }
            catch( const function_exception& e )
            {
                if( e.is_passthrough() )
                {
                    throw;
                }
            }

            return no_value;
        }

        auto operator()( Args&&... args ) const -> return_t
        {
            try
            {
                if( this->valid() )
                {
                    return return_t{ this->slot_.func(std::move(args)...) };
                }
            }
            catch( const function_exception& e )
            {
                if( e.is_passthrough() )
                {
                    throw;
                }
            }

            return no_value;
        }
    };

    // Template base
    template<typename Function, typename Enabler = void>
    struct function;

// Void functions

    // Generic void function
    template<typename ReturnType, typename... Args>
    struct function<ReturnType(Args...), returns_void<ReturnType>>
        : _void_function_base<Args...>
    {
        template<class Class>
        using mem_func_t = void(Class::*const)(Args...);

        template<class Class>
        using const_mem_func_t = void(Class::*const)(Args...) const;

        template<class Class>
        void connect( const sentinel_opt_t& sentinel,
                      Class* const object_ptr,
                      const mem_func_t<Class>& method_ptr )
        {
            this->connect_impl( sentinel, [object_ptr, method_ptr](auto&&... args){ return (object_ptr->*method_ptr)(std::forward<Args>(args)...); } );
        }

        template<class Class>
        void connect( const sentinel_opt_t& sentinel,
                      const Class* const object_ptr,
                      const const_mem_func_t<Class>& method_ptr )
        {
            this->connect_impl( sentinel, [object_ptr, method_ptr](auto&&... args){ return (object_ptr->*method_ptr)(std::forward<Args>(args)...); } );
        }

        template<typename Sentinel, typename Function, typename = _sentinel_function_enabler<function, Function, Sentinel>>
        void connect( const Sentinel& sentinel,
                      Function&& f )
        {
            this->connect_impl( sentinel, std::forward<Function>(f) );
        }

        template<class Class>
        void connect( Class* const object_ptr,
                      const mem_func_t<Class>& method_ptr )
        {
            connect( no_value, object_ptr, method_ptr );
        }

        template<class Class>
        void connect( const Class* const object_ptr,
                      const const_mem_func_t<Class>& method_ptr )
        {
            connect( no_value, object_ptr, method_ptr );
        }

        template<typename Function, typename = _function_enabler<function, Function>>
        void connect( Function&& f )
        {
            connect( no_value, std::forward<Function>(f) );
        }

        template<typename Function, typename = _function_enabler<function, Function>>
        auto operator=( Function&& f ) -> function&
        {
            connect( this->slot_.sentinel, std::forward<Function>(f) );
            return *this;
        }

        function() = default;

        template<class Class>
        function( const sentinel_opt_t& sentinel,
                  Class* const object_ptr,
                  const mem_func_t<Class>& method_ptr )
        {
            connect( sentinel, object_ptr, method_ptr );
        }

        template<class Class>
        function( const sentinel_opt_t& sentinel,
                  const Class* const object_ptr,
                  const const_mem_func_t<Class>& method_ptr )
        {
            connect( sentinel, object_ptr, method_ptr );
        }

        template<typename Sentinel, typename Function, typename = _sentinel_function_enabler<function, Function, Sentinel>>
        function( const Sentinel& sentinel,
                  Function&& f )
        {
           connect( sentinel, std::forward<Function>(f) );
        }

        template<class Class>
        function( Class* const object_ptr,
                  const mem_func_t<Class>& method_ptr )
        {
           connect( no_value, object_ptr, method_ptr );
        }

        template<class Class>
        function( const Class* const object_ptr,
                  const const_mem_func_t<Class>& method_ptr )
        {
           connect( no_value, object_ptr, method_ptr );
        }

        template<typename Function, typename = _function_enabler<function, Function>>
        function( Function&& f )
        {
           connect( no_value, std::forward<Function>(f) );
        }
    };

    // Void function reference
    template<typename ReturnType, typename... Args>
    struct function<ReturnType(*)(Args...), returns_void<ReturnType>>
        : _void_function_base<Args...>
    {

        template<typename Sentinel, typename Function, typename = _sentinel_function_enabler<function, Function, Sentinel>>
        void connect( const Sentinel& sentinel,
                      Function&& f )
        {
            this->connect_impl( sentinel, std::forward<Function>(f) );
        }

        template<typename Function, typename = _function_enabler<function, Function>>
        void connect( Function&& f )
        {
            connect( no_value, std::forward<Function>(f) );
        }

        function() = default;

        template<typename Sentinel, typename Function, typename = _sentinel_function_enabler<function, Function, Sentinel>>
        function( const Sentinel& sentinel,
                  Function&& f )
        {
            connect( sentinel, std::forward<Function>(f) );
        }

        template<typename Function, typename = _function_enabler<function, Function>>
        function( Function&& f )
        {
            connect( no_value, std::forward<Function>(f) );
        }
    };

    // Member void function reference
    template<typename ReturnType, typename Class, typename... Args>
    struct function<ReturnType(Class::*)(Args...), returns_void<ReturnType>>
        : _void_function_base<Args...>
    {
        using mem_func_t = void(Class::*const)(Args...);

        void connect( const sentinel_opt_t& sentinel,
                      Class* const object_ptr,
                      const mem_func_t& method_ptr )
        {
            this->connect_impl( sentinel, [object_ptr, method_ptr](auto&&... args){ return (object_ptr->*method_ptr)(std::forward<Args>(args)...); } );
        }

        void connect( Class* const object_ptr,
                      const mem_func_t& method_ptr )
        {
            connect( no_value, object_ptr, method_ptr );
        }

        function() = default;

        function( const sentinel_opt_t& sentinel,
                  Class* const object_ptr,
                  const mem_func_t& method_ptr )
        {
            connect( sentinel, object_ptr, method_ptr );
        }

        function( Class* const object_ptr,
                  const mem_func_t& method_ptr )
        {
            connect( no_value, object_ptr, method_ptr );
        }
    };

    // Member const void function reference
    template<typename ReturnType, typename Class, typename... Args>
    struct function<ReturnType(Class::*)(Args...) const, returns_void<ReturnType>>
        : _void_function_base<Args...>
    {
        using const_mem_func_t = void(Class::*const)(Args...) const;

        void connect( const sentinel_opt_t& sentinel,
                      const Class* const object_ptr,
                      const const_mem_func_t& method_ptr )
        {
            this->connect_impl( sentinel, [object_ptr, method_ptr](auto&&... args){ return (object_ptr->*method_ptr)(std::forward<Args>(args)...); } );
        }

        void connect( const Class* const object_ptr,
                      const const_mem_func_t& method_ptr )
        {
            connect( no_value, object_ptr, method_ptr );
        }

        function() = default;

        function( const sentinel_opt_t& sentinel,
                  const Class* const object_ptr,
                  const const_mem_func_t& method_ptr )
        {
            connect( sentinel, object_ptr, method_ptr );
        }

        function( const Class* const object_ptr,
                  const const_mem_func_t& method_ptr )
        {
            connect( no_value, object_ptr, method_ptr );
        }
    };

// Value functions

    // Generic function
    template<typename ReturnType, typename... Args>
    struct function<ReturnType(Args...), returns_value<ReturnType>>
        : _value_function_base<ReturnType, Args...>
    {
        template<class Class>
        using mem_func_t = ReturnType(Class::*const)(Args...);

        template<class Class>
        using const_mem_func_t = ReturnType(Class::*const)(Args...) const;

        template<class Class>
        void connect( const sentinel_opt_t& sentinel,
                      Class* const object_ptr,
                      const mem_func_t<Class>& method_ptr )
        {
            this->connect_impl( sentinel, [object_ptr, method_ptr](auto&&... args){ return (object_ptr->*method_ptr)(std::forward<Args>(args)...); } );
        }

        template<class Class>
        void connect( const sentinel_opt_t& sentinel,
                      const Class* const object_ptr,
                      const const_mem_func_t<Class>& method_ptr )
        {
            this->connect_impl( sentinel, [object_ptr, method_ptr](auto&&... args){ return (object_ptr->*method_ptr)(std::forward<Args>(args)...); } );
        }

        template<typename Sentinel, typename Function, typename = _sentinel_function_enabler<function, Function, Sentinel>>
        void connect( const Sentinel& sentinel,
                      Function&& f )
        {
            this->connect_impl( sentinel, std::forward<Function>(f) );
        }

        template<class Class>
        void connect( Class* const object_ptr,
                      const mem_func_t<Class>& method_ptr )
        {
            connect( no_value, object_ptr, method_ptr );
        }

        template<class Class>
        void connect( const Class* const object_ptr,
                      const const_mem_func_t<Class>& method_ptr )
        {
            connect( no_value, object_ptr, method_ptr );
        }

        template<typename Function, typename = _function_enabler<function, Function>>
        void connect( Function&& f )
        {
            connect( no_value, std::forward<Function>(f) );
        }

        template<typename Function, typename = _function_enabler<function, Function>>
        auto operator=( Function&& f ) -> function&
        {
            connect( this->slot_.sentinel, std::forward<Function>(f) );
            return *this;
        }

        function() = default;

        template<class Class>
        function( const sentinel_opt_t& sentinel,
                  Class* const object_ptr,
                  const mem_func_t<Class>& method_ptr )
        {
            connect( sentinel, object_ptr, method_ptr );
        }

        template<class Class>
        function( const sentinel_opt_t& sentinel,
                  const Class* const object_ptr,
                  const const_mem_func_t<Class>& method_ptr )
        {
            connect( sentinel, object_ptr, method_ptr );
        }

        template<typename Sentinel, typename Function, typename = _sentinel_function_enabler<function, Function, Sentinel>>
        function( const Sentinel& sentinel,
                  Function&& f )
        {
            connect( sentinel, std::forward<Function>(f) );
        }

        template<class Class>
        function( Class* const object_ptr,
                  const mem_func_t<Class>& method_ptr )
        {
            connect( no_value, object_ptr, method_ptr );
        }

        template<class Class>
        function( const Class* const object_ptr,
                  const const_mem_func_t<Class>& method_ptr )
        {
            connect( no_value, object_ptr, method_ptr );
        }

        template<typename Function, typename = _function_enabler<function, Function>>
        function( Function&& f )
        {
            connect( no_value, std::forward<Function>(f) );
        }
    };

    // Function reference
    template<typename ReturnType, typename... Args>
    struct function<ReturnType(*)(Args...), returns_value<ReturnType>>
        : _value_function_base<ReturnType, Args...>
    {
        template<typename Sentinel, typename Function, typename = _sentinel_function_enabler<function, Function, Sentinel>>
        void connect( const Sentinel& sentinel,
                      Function&& f )
        {
            this->connect_impl( sentinel, std::forward<Function>(f) );
        }

        template<typename Function, typename = _function_enabler<function, Function>>
        void connect( Function&& f )
        {
            connect( no_value, std::forward<Function>(f) );
        }

        function() = default;

        template<typename Sentinel, typename Function, typename = _sentinel_function_enabler<function, Function, Sentinel>>
        function( const Sentinel& sentinel,
                  Function&& f )
        {
            connect( sentinel, std::forward<Function>(f) );
        }

        template<typename Function, typename = _function_enabler<function, Function>>
        function( Function&& f )
        {
            connect( no_value, std::forward<Function>(f) );
        }
    };

    // Member function reference
    template<typename ReturnType, typename Class, typename... Args>
    struct function<ReturnType(Class::*)(Args...), returns_value<ReturnType>>
        : _value_function_base<ReturnType, Args...>
    {
        using mem_func_t = ReturnType(Class::*)(Args...);

        void connect( const sentinel_opt_t& sentinel,
                      Class* const object_ptr,
                      const mem_func_t& method_ptr )
        {
            this->connect_impl( sentinel, [object_ptr, method_ptr](auto&&... args){ return (object_ptr->*method_ptr)(std::forward<Args>(args)...); } );
        }

        void connect( Class* const object_ptr,
                      const mem_func_t& method_ptr )
        {
            connect( no_value, object_ptr, method_ptr );
        }

        function() = default;

        function( const sentinel_opt_t& sentinel,
                  Class* const object_ptr,
                  const mem_func_t& method_ptr )
        {
            connect( sentinel, object_ptr, method_ptr );
        }

        function( Class* const object_ptr,
                  const mem_func_t& method_ptr )
        {
            connect( no_value, object_ptr, method_ptr );
        }
    };

    // Member const function reference
    template<typename ReturnType, typename Class, typename... Args>
    struct function<ReturnType(Class::*)(Args...) const, returns_value<ReturnType>>
        : _value_function_base<ReturnType, Args...>
    {
        using const_mem_func_t = ReturnType(Class::*const)(Args...) const;

        void connect( const sentinel_opt_t& sentinel,
                      const Class* const object_ptr,
                      const const_mem_func_t& method_ptr )
        {
            this->connect_impl( sentinel, [object_ptr, method_ptr](auto&&... args){ return (object_ptr->*method_ptr)(std::forward<Args>(args)...); } );
        }

        void connect( const Class* const object_ptr,
                      const const_mem_func_t& method_ptr )
        {
            connect( no_value, object_ptr, method_ptr );
        }

        function() = default;

        function( const sentinel_opt_t& sentinel,
                  const Class* const object_ptr,
                  const const_mem_func_t& method_ptr )
        {
            connect( sentinel, object_ptr, method_ptr );
        }

        function( const Class* const object_ptr,
                  const const_mem_func_t& method_ptr )
        {
            connect( no_value, object_ptr, method_ptr );
        }
    };
} // namespace hpp
