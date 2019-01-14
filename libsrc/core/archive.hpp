#ifndef NETGEN_CORE_ARCHIVE_HPP
#define NETGEN_CORE_ARCHIVE_HPP

#include <complex>              // for complex
#include <cstring>              // for size_t, strlen
#include <fstream>              // for ifstream, ofstream
#include <functional>           // for function
#include <map>                  // for map
#include <memory>               // for shared_ptr
#include <string>               // for string
#include <type_traits>          // for declval, enable_if, false_type, is_co...
#include <typeinfo>             // for type_info
#include <utility>              // for move, swap, pair
#include <vector>               // for vector

#include "exception.hpp"        // for UnreachableCodeException, Exception
#include "logging.hpp"          // for logger
#include "ngcore_api.hpp"       // for NGCORE_API
#include "type_traits.hpp"      // for all_of_tmpl
#include "utils.hpp"            // for Demangle, unlikely
#include "version.hpp"          // for VersionInfo

#ifdef NETGEN_PYTHON
#include <pybind11/pybind11.h>
#endif // NETGEN_PYTHON

namespace ngcore
{
  // Libraries using this archive can store their version here to implement backwards compatibility
  NGCORE_API const VersionInfo& GetLibraryVersion(const std::string& library);
  NGCORE_API void SetLibraryVersion(const std::string& library, const VersionInfo& version);

  class NGCORE_API Archive;

  namespace detail
  {
    // create new pointer of type T if it is default constructible, else throw
    template<typename T, typename ...Rest>
    T* constructIfPossible_impl(Rest... /*unused*/)
    { throw Exception(std::string(Demangle(typeid(T).name())) + " is not default constructible!"); }

    template<typename T, typename= typename std::enable_if<std::is_constructible<T>::value>::type>
    T* constructIfPossible_impl(int /*unused*/) { return new T; } // NOLINT

    template<typename T>
    T* constructIfPossible() { return constructIfPossible_impl<T>(int{}); }

    //Type trait to check if a class implements a 'void DoArchive(Archive&)' function
    template<typename T>
    struct has_DoArchive
    {
    private:
      template<typename T2>
      static constexpr auto check(T2*) ->
        typename std::is_same<decltype(std::declval<T2>().DoArchive(std::declval<Archive&>())),void>::type;
      template<typename>
      static constexpr std::false_type check(...);
      using type = decltype(check<T>(nullptr)); // NOLINT
    public:
      NGCORE_API static constexpr bool value = type::value;
    };

    // Check if class is archivable
    template<typename T>
    struct is_Archivable_struct
    {
    private:
      template<typename T2>
      static constexpr auto check(T2*) ->
        typename std::is_same<decltype(std::declval<Archive>() & std::declval<T2&>()),Archive&>::type;
      template<typename>
      static constexpr std::false_type check(...);
      using type = decltype(check<T>(nullptr)); // NOLINT
    public:
      NGCORE_API static constexpr bool value = type::value;
    };

    struct ClassArchiveInfo
    {
      // create new object of this type and return a void* pointer that is points to the location
      // of the (base)class given by type_info
      std::function<void*(const std::type_info&)> creator;
      // This caster takes a void* pointer to the type stored in this info and casts it to a
      // void* pointer pointing to the (base)class type_info
      std::function<void*(const std::type_info&, void*)> upcaster;
      // This caster takes a void* pointer to the (base)class type_info and returns void* pointing
      // to the type stored in this info
      std::function<void*(const std::type_info&, void*)> downcaster;
    };
  } // namespace detail

  template<typename T>
  constexpr bool is_archivable = detail::is_Archivable_struct<T>::value;

  // Base Archive class
  class NGCORE_API Archive
  {
    const bool is_output;
    // how many different shared_ptr/pointer have been (un)archived
    int shared_ptr_count{0}, ptr_count{0};
    // maps for archived shared pointers and pointers
    std::map<void*, int> shared_ptr2nr{}, ptr2nr{};
    // vectors for storing the unarchived (shared) pointers
    std::vector<std::shared_ptr<void>> nr2shared_ptr{};
    std::vector<void*> nr2ptr{};
  protected:
    bool shallow_to_python = false;
    std::map<std::string, VersionInfo> version_map = GetLibraryVersions();
    std::shared_ptr<spdlog::logger> logger = GetLogger("Archive");
  public:
    Archive() = delete;
    Archive(const Archive&) = delete;
    Archive(Archive&&) = delete;
    Archive (bool ais_output) : is_output(ais_output) { ; }

    virtual ~Archive() { ; }

    // If the object is pickled, all shallow archived objects will be pickled as a list,
    // instead of written as a binary archive. This allows pickle to serialize every object only
    // once and put them together correctly afterwards. Therefore all objects that may live in
    // Python should be archived using this Shallow function. If Shallow is called from C++ code
    // it archives the object normally.
    template<typename T>
    Archive& Shallow(T& val)
    {
      static_assert(detail::is_any_pointer<T>, "ShallowArchive must be given pointer type!");
#ifdef NETGEN_PYTHON
      if(shallow_to_python)
        {
          if(is_output)
            ShallowOutPython(pybind11::cast(val));
          else
            val = pybind11::cast<T>(ShallowInPython());
        }
      else
#endif // NETGEN_PYTHON
        *this & val;
      return *this;
    }

#ifdef NETGEN_PYTHON
    virtual void ShallowOutPython(const pybind11::object& /*unused*/)
    { throw UnreachableCodeException{}; }
    virtual pybind11::object ShallowInPython()
    { throw UnreachableCodeException{}; }
#endif // NETGEN_PYTHON

    Archive& operator=(const Archive&) = delete;
    Archive& operator=(Archive&&) = delete;

    bool Output () const { return is_output; }
    bool Input () const { return !is_output; }
    const VersionInfo& GetVersion(const std::string& library)
    { return version_map[library]; }

    // Pure virtual functions that have to be implemented by In-/OutArchive
    virtual Archive & operator & (double & d) = 0;
    virtual Archive & operator & (int & i) = 0;
    virtual Archive & operator & (long & i) = 0;
    virtual Archive & operator & (size_t & i) = 0;
    virtual Archive & operator & (short & i) = 0;
    virtual Archive & operator & (unsigned char & i) = 0;
    virtual Archive & operator & (bool & b) = 0;
    virtual Archive & operator & (std::string & str) = 0;
    virtual Archive & operator & (char *& str) = 0;

    Archive & operator & (VersionInfo & version)
    {
        if(Output())
            (*this) << version.to_string();
        else
        {
            std::string s;
            (*this) & s;
            version = VersionInfo(s);
        }
        return *this;
    }

    // Archive std classes ================================================
    template<typename T>
    Archive& operator & (std::complex<T>& c)
    {
      if(Output())
          (*this) << c.real() << c.imag();
      else
        {
          T tmp;
          (*this) & tmp;
          c.real(tmp);
          (*this) & tmp;
          c.imag(tmp);
        }
      return (*this);
    }
    template<typename T>
    Archive& operator & (std::vector<T>& v)
    {
      size_t size;
      if(Output())
          size = v.size();
      (*this) & size;
      if(Input())
        v.resize(size);
      Do(&v[0], size);
      return (*this);
    }

    // vector<bool> has special implementation (like a bitarray) therefore
    // it needs a special overload (this could probably be more efficient, but we
    // don't use it that often anyway)
    Archive& operator& (std::vector<bool>& v)
    {
      size_t size;
      if(Output())
        size = v.size();
      (*this) & size;
      if(Input())
        {
          v.resize(size);
          bool b;
          for(size_t i=0; i<size; i++)
            {
              (*this) & b;
              v[i] = b;
            }
        }
      else
        {
          for(bool b : v)
            (*this) & b;
        }
      return *this;
    }
    template<typename T1, typename T2>
    Archive& operator& (std::map<T1, T2>& map)
    {
      if(Output())
        {
          (*this) << size_t(map.size());
          for(auto& pair : map)
              (*this) << pair.first << pair.second;
        }
      else
        {
          size_t size = 0;
          (*this) & size;
          T1 key; T2 val;
          for(size_t i = 0; i < size; i++)
            {
              T1 key; T2 val;
              (*this) & key & val;
              map[key] = val;
            }
        }
      return (*this);
    }
    // Archive arrays =====================================================
    // this functions can be overloaded in Archive implementations for more efficiency
    template <typename T, typename = typename std::enable_if<is_archivable<T>>::type>
    Archive & Do (T * data, size_t n)
    { for (size_t j = 0; j < n; j++) { (*this) & data[j]; }; return *this; }; // NOLINT

    virtual Archive & Do (double * d, size_t n)
    { for (size_t j = 0; j < n; j++) { (*this) & d[j]; }; return *this; }; // NOLINT

    virtual Archive & Do (int * i, size_t n)
    { for (size_t j = 0; j < n; j++) { (*this) & i[j]; }; return *this; }; // NOLINT

    virtual Archive & Do (long * i, size_t n)
    { for (size_t j = 0; j < n; j++) { (*this) & i[j]; }; return *this; }; // NOLINT

    virtual Archive & Do (size_t * i, size_t n)
    { for (size_t j = 0; j < n; j++) { (*this) & i[j]; }; return *this; }; // NOLINT

    virtual Archive & Do (short * i, size_t n)
    { for (size_t j = 0; j < n; j++) { (*this) & i[j]; }; return *this; }; // NOLINT

    virtual Archive & Do (unsigned char * i, size_t n)
    { for (size_t j = 0; j < n; j++) { (*this) & i[j]; }; return *this; }; // NOLINT

    virtual Archive & Do (bool * b, size_t n)
    { for (size_t j = 0; j < n; j++) { (*this) & b[j]; }; return *this; }; // NOLINT

    // Archive a class implementing a (void DoArchive(Archive&)) method =======
    template<typename T, typename=std::enable_if_t<detail::has_DoArchive<T>::value>>
    Archive& operator & (T& val)
    {
      val.DoArchive(*this); return *this;
    }

    // Archive shared_ptrs =================================================
    template <typename T>
    Archive& operator & (std::shared_ptr<T>& ptr)
    {
      if(Output())
        {
          NETGEN_DEBUG_LOG(logger, "Store shared ptr of type " + Demangle(typeid(T).name()));
          // save -2 for nullptr
          if(!ptr)
            {
              NETGEN_DEBUG_LOG(logger, "Storing nullptr");
              return (*this) << -2;
            }

          void* reg_ptr = ptr.get();
          bool neededDowncast = false;
          // Downcasting is only possible for our registered classes
          if(typeid(T) != typeid(*ptr))
            {
              NETGEN_DEBUG_LOG(logger, "Typids are different: " + Demangle(typeid(T).name()) + " vs. " +
                               Demangle(typeid(*ptr).name()));
              if(!IsRegistered(Demangle(typeid(*ptr).name())))
                  throw Exception(std::string("Archive error: Polymorphic type ")
                                  + Demangle(typeid(*ptr).name())
                                  + " not registered for archive");
              reg_ptr = GetArchiveRegister(Demangle(typeid(*ptr).name())).downcaster(typeid(T), ptr.get());
              // if there was a true downcast we have to store more information
              if(reg_ptr != static_cast<void*>(ptr.get()))
                {
                  NETGEN_DEBUG_LOG(logger, "Multiple/Virtual inheritance involved, need to cast pointer");
                  neededDowncast = true;
                }
            }
          auto pos = shared_ptr2nr.find(reg_ptr);
          // if not found store -1 and the pointer
          if(pos == shared_ptr2nr.end())
            {
              NETGEN_DEBUG_LOG(logger, "Didn't find the shared_ptr, create new registry entry at " +
                               std::to_string(shared_ptr_count));
              auto p = ptr.get();
              (*this) << -1;
              (*this) & neededDowncast & p;
              // if we did downcast we store the true type as well
              if(neededDowncast)
                (*this) << Demangle(typeid(*ptr).name());
              shared_ptr2nr[reg_ptr] = shared_ptr_count++;
              return *this;
            }
          // if found store the position and if it has to be downcasted and how
          NETGEN_DEBUG_LOG(logger, "Found shared_ptr at position " + std::to_string(pos->second));
          (*this) << pos->second << neededDowncast;
          if(neededDowncast)
            (*this) << Demangle(typeid(*ptr).name());
        }
      else // Input
        {
          NETGEN_DEBUG_LOG(logger, "Reading shared_ptr of type " + Demangle(typeid(T).name()));
          int nr;
          (*this) & nr;
          // -2 restores a nullptr
          if(nr == -2)
            {
              NETGEN_DEBUG_LOG(logger, "Reading a nullptr");
              ptr = nullptr;
              return *this;
            }
          // -1 restores a new shared ptr by restoring the inner pointer and creating a shared_ptr to it
          if (nr == -1)
            {
              NETGEN_DEBUG_LOG(logger, "Createing new shared_ptr");
              T* p = nullptr;
              bool neededDowncast;
              (*this) & neededDowncast & p;
              ptr = std::shared_ptr<T>(p);
              // if we did downcast we need to store a shared_ptr<void> to the true object
              if(neededDowncast)
                {
                  NETGEN_DEBUG_LOG(logger, "Shared pointer needed downcasting");
                  std::string name;
                  (*this) & name;
                  auto info = GetArchiveRegister(name);
                  // for this we use an aliasing constructor to create a shared pointer sharing lifetime
                  // with our shared ptr, but pointing to the true object
                  nr2shared_ptr.push_back(std::shared_ptr<void>(std::static_pointer_cast<void>(ptr),
                                                                info.downcaster(typeid(T),
                                                                                ptr.get())));
                }
              else
                {
                  NETGEN_DEBUG_LOG(logger, "Shared pointer didn't need downcasting");
                  nr2shared_ptr.push_back(ptr);
                }
            }
          else
            {
              NETGEN_DEBUG_LOG(logger, "Reading already existing pointer at entry " + std::to_string(nr));
              auto other = nr2shared_ptr[nr];
              bool neededDowncast;
              (*this) & neededDowncast;
              if(neededDowncast)
                {
                  NETGEN_DEBUG_LOG(logger, "Shared pointer needed pointer downcast");
                  // if there was a downcast we can expect the class to be registered (since archiving
                  // wouldn't have worked else)
                  std::string name;
                  (*this) & name;
                  auto info = GetArchiveRegister(name);
                  // same trick as above, create a shared ptr sharing lifetime with
                  // the shared_ptr<void> in the register, but pointing to our object
                  ptr = std::static_pointer_cast<T>(std::shared_ptr<void>(other,
                                                                          info.upcaster(typeid(T),
                                                                               other.get())));
                }
              else
                {
                  NETGEN_DEBUG_LOG(logger, "Shared pointer didn't need pointer casts");
                  ptr = std::static_pointer_cast<T>(other);
                }
            }
        }
      return *this;
    }

    // Archive pointers =======================================================
    template <typename T>
    Archive & operator& (T *& p)
    {
      if (Output())
        {
          NETGEN_DEBUG_LOG(logger, "Store pointer of type " + Demangle(typeid(T).name()));
          // if the pointer is null store -2
          if (!p)
            {
              NETGEN_DEBUG_LOG(logger, "Storing nullptr");
              return (*this) << -2;
            }
          auto reg_ptr = static_cast<void*>(p);
          if(typeid(T) != typeid(*p))
            {
              NETGEN_DEBUG_LOG(logger, "Typeids are different: " + Demangle(typeid(T).name()) + " vs. " +
                               Demangle(typeid(*p).name()));
              if(!IsRegistered(Demangle(typeid(*p).name())))
                throw Exception(std::string("Archive error: Polymorphic type ")
                                + Demangle(typeid(*p).name())
                                + " not registered for archive");
              reg_ptr = GetArchiveRegister(Demangle(typeid(*p).name())).downcaster(typeid(T), static_cast<void*>(p));
              if(reg_ptr != static_cast<void*>(p))
                {
                  NETGEN_DEBUG_LOG(logger, "Multiple/Virtual inheritance involved, need to cast pointer");
                }
            }
          auto pos = ptr2nr.find(reg_ptr);
          // if the pointer is not found in the map create a new entry
          if (pos == ptr2nr.end())
            {
              NETGEN_DEBUG_LOG(logger, "Didn't find pointer, create new registry entry at " +
                               std::to_string(ptr_count));
              ptr2nr[reg_ptr] = ptr_count++;
              if(typeid(*p) == typeid(T))
                if (std::is_constructible<T>::value)
                               {
                                 NETGEN_DEBUG_LOG(logger, "Store standard class pointer (no virt. inh,...)");
                                 return (*this) << -1 & (*p);
                               }
                else
                  throw Exception(std::string("Archive error: Class ") +
                                  Demangle(typeid(*p).name()) + " does not provide a default constructor!");
              else
                {
                  // if a pointer to a base class is archived, the class hierarchy must be registered
                  // to avoid compile time issues we allow this behaviour only for "our" classes that
                  // implement a void DoArchive(Archive&) member function
                  // To recreate the object we need to store the true type of it
                  if(!IsRegistered(Demangle(typeid(*p).name())))
                    throw Exception(std::string("Archive error: Polymorphic type ")
                                    + Demangle(typeid(*p).name())
                                    + " not registered for archive");
                  NETGEN_DEBUG_LOG(logger, "Store a possibly more complicated pointer");
                  return (*this) << -3 << Demangle(typeid(*p).name()) & (*p);
                }
            }
          else
            {
              (*this) & pos->second;
              bool downcasted = !(reg_ptr == static_cast<void*>(p) );
              NETGEN_DEBUG_LOG(logger, "Store a the existing position in registry at " +
                               std::to_string(pos->second));
              NETGEN_DEBUG_LOG(logger, std::string("Pointer ") + (downcasted ? "needs " : "doesn't need ") +
                               "downcasting");
              // store if the class has been downcasted and the name
              (*this) << downcasted << Demangle(typeid(*p).name());
            }
        }
      else
        {
          NETGEN_DEBUG_LOG(logger, "Reading pointer of type " + Demangle(typeid(T).name()));
          int nr;
          (*this) & nr;
          if (nr == -2) // restore a nullptr
            {
              NETGEN_DEBUG_LOG(logger, "Loading a nullptr");
              p = nullptr;
            }
          else if (nr == -1) // create a new pointer of standard type (no virtual or multiple inheritance,...)
            {
              NETGEN_DEBUG_LOG(logger, "Load a new pointer to a simple class");
              p = detail::constructIfPossible<T>();
              nr2ptr.push_back(p);
              (*this) & *p;
            }
          else if(nr == -3) // restore one of our registered classes that can have multiple inheritance,...
            {
              NETGEN_DEBUG_LOG(logger, "Load a new pointer to a potentially more complicated class "
                               "(allows for multiple/virtual inheritance,...)");
              // As stated above, we want this special behaviour only for our classes that implement DoArchive
              std::string name;
              (*this) & name;
              NETGEN_DEBUG_LOG(logger, "Name = " + name);
              auto info = GetArchiveRegister(name);
              // the creator creates a new object of type name, and returns a void* pointing
              // to T (which may have an offset)
              p = static_cast<T*>(info.creator(typeid(T)));
              // we store the downcasted pointer (to be able to find it again from
              // another class in a multiple inheritance tree)
              nr2ptr.push_back(info.downcaster(typeid(T),p));
              (*this) & *p;
            }
          else
            {
              NETGEN_DEBUG_LOG(logger, "Restoring pointer to already existing object at registry position " +
                               std::to_string(nr));
              bool downcasted;
              std::string name;
              (*this) & downcasted & name;
              NETGEN_DEBUG_LOG(logger, std::string(downcasted ? "Downcasted" : "Not downcasted") +
                               " object of type " + name);
              if(downcasted)
                {
                  // if the class has been downcasted we can assume it is in the register
                  auto info = GetArchiveRegister(name);
                  p = static_cast<T*>(info.upcaster(typeid(T), nr2ptr[nr]));
                }
              else
                p = static_cast<T*>(nr2ptr[nr]);
            }
        }
      return *this;
    }

    // const ptr
    template<typename T>
    Archive& operator &(const T*& t)
    {
      return (*this) & const_cast<T*&>(t); // NOLINT
    }

    // Write a read only variable
    template <typename T>
    Archive & operator << (const T & t)
    {
      T ht(t);
      (*this) & ht;
      return *this;
    }

    virtual void FlushBuffer() {}

  protected:
    static std::map<std::string, VersionInfo>& GetLibraryVersions();

  private:
    template<typename T, typename ... Bases>
    friend class RegisterClassForArchive;

    // Returns ClassArchiveInfo of Demangled typeid
    static const detail::ClassArchiveInfo& GetArchiveRegister(const std::string& classname);
    // Set ClassArchiveInfo for Demangled typeid, this is done by creating an instance of
    // RegisterClassForArchive<type, bases...>
    static void SetArchiveRegister(const std::string& classname, const detail::ClassArchiveInfo& info);
    static bool IsRegistered(const std::string& classname);

    // Helper class for up-/downcasting
    template<typename T, typename ... Bases>
    struct Caster{};

    template<typename T>
    struct Caster<T>
    {
      static void* tryUpcast (const std::type_info& /*unused*/, T* /*unused*/)
      {
        throw Exception("Upcast not successful, some classes are not registered properly for archiving!");
      }
      static void* tryDowncast (const std::type_info& /*unused*/, void* /*unused*/)
      {
        throw Exception("Downcast not successful, some classes are not registered properly for archiving!");
      }
    };

    template<typename T, typename B1, typename ... Brest>
    struct Caster<T,B1,Brest...>
    {
      static void* tryUpcast(const std::type_info& ti, T* p)
      {
        try
          { return GetArchiveRegister(Demangle(typeid(B1).name())).
              upcaster(ti, static_cast<void*>(dynamic_cast<B1*>(p))); }
        catch(const Exception&)
          { return Caster<T, Brest...>::tryUpcast(ti, p); }
      }

      static void* tryDowncast(const std::type_info& ti, void* p)
      {
        if(typeid(B1) == ti)
          return dynamic_cast<T*>(static_cast<B1*>(p));
        try
          {
            return dynamic_cast<T*>(static_cast<B1*>(GetArchiveRegister(Demangle(typeid(B1).name())).
                                                     downcaster(ti, p)));
          }
        catch(const Exception&)
          {
            return Caster<T, Brest...>::tryDowncast(ti, p);
          }
      }
    };
  };

  template<typename T, typename ... Bases>
  class RegisterClassForArchive
  {
  public:
    RegisterClassForArchive()
    {
      static_assert(detail::all_of_tmpl<std::is_base_of<Bases,T>::value...>,
                    "Variadic template arguments must be base classes of T");
      detail::ClassArchiveInfo info {};
      info.creator = [this,&info](const std::type_info& ti) -> void*
                     { return typeid(T) == ti ? detail::constructIfPossible<T>()
                         : Archive::Caster<T, Bases...>::tryUpcast(ti, detail::constructIfPossible<T>()); };
      info.upcaster = [this](const std::type_info& ti, void* p) -> void*
                      { return typeid(T) == ti ? p : Archive::Caster<T, Bases...>::tryUpcast(ti, static_cast<T*>(p)); };
      info.downcaster = [this](const std::type_info& ti, void* p) -> void*
                        { return typeid(T) == ti ? p : Archive::Caster<T, Bases...>::tryDowncast(ti, p); };
      Archive::SetArchiveRegister(std::string(Demangle(typeid(T).name())),info);
    }


  };

  // BinaryOutArchive ======================================================================
  class NGCORE_API BinaryOutArchive : public Archive
  {
    static constexpr size_t BUFFERSIZE = 1024;
    char buffer[BUFFERSIZE] = {};
    size_t ptr = 0;
  protected:
    std::shared_ptr<std::ostream> stream;
  public:
    BinaryOutArchive() = delete;
    BinaryOutArchive(const BinaryOutArchive&) = delete;
    BinaryOutArchive(BinaryOutArchive&&) = delete;
    BinaryOutArchive(std::shared_ptr<std::ostream>&& astream)
      : Archive(true), stream(std::move(astream))
    { }
    BinaryOutArchive(const std::string& filename)
      : BinaryOutArchive(std::make_shared<std::ofstream>(filename)) {}
    ~BinaryOutArchive () override { FlushBuffer(); }

    BinaryOutArchive& operator=(const BinaryOutArchive&) = delete;
    BinaryOutArchive& operator=(BinaryOutArchive&&) = delete;

    using Archive::operator&;
    Archive & operator & (double & d) override
    { return Write(d); }
    Archive & operator & (int & i) override
    { return Write(i); }
    Archive & operator & (short & i) override
    { return Write(i); }
    Archive & operator & (long & i) override
    { return Write(i); }
    Archive & operator & (size_t & i) override
    { return Write(i); }
    Archive & operator & (unsigned char & i) override
    { return Write(i); }
    Archive & operator & (bool & b) override
    { return Write(b); }
    Archive & operator & (std::string & str) override
    {
      int len = str.length();
      (*this) & len;
      FlushBuffer();
      if(len)
        stream->write (&str[0], len);
      return *this;
    }
    Archive & operator & (char *& str) override
    {
      long len = str ? strlen (str) : -1;
      (*this) & len;
      FlushBuffer();
      if(len > 0)
        stream->write (&str[0], len); // NOLINT
      return *this;
    }
    void FlushBuffer() override
    {
      if (ptr > 0)
        {
          stream->write(&buffer[0], ptr);
          ptr = 0;
        }
    }

  private:
    template <typename T>
    Archive & Write (T x)
    {
      if (unlikely(ptr > BUFFERSIZE-sizeof(T)))
        {
          stream->write(&buffer[0], ptr);
          *reinterpret_cast<T*>(&buffer[0]) = x; // NOLINT
          ptr = sizeof(T);
          return *this;
        }
      *reinterpret_cast<T*>(&buffer[ptr]) = x; // NOLINT
      ptr += sizeof(T);
      return *this;
    }
  };

  // BinaryInArchive ======================================================================
  class NGCORE_API BinaryInArchive : public Archive
  {
  protected:
    std::shared_ptr<std::istream> stream;
  public:
    BinaryInArchive (std::shared_ptr<std::istream>&& astream)
      : Archive(false), stream(std::move(astream))
    { }
    BinaryInArchive (const std::string& filename)
      : BinaryInArchive(std::make_shared<std::ifstream>(filename)) { ; }

    using Archive::operator&;
    Archive & operator & (double & d) override
    { Read(d); return *this; }
    Archive & operator & (int & i) override
    { Read(i); return *this; }
    Archive & operator & (short & i) override
    { Read(i); return *this; }
    Archive & operator & (long & i) override
    { Read(i); return *this; }
    Archive & operator & (size_t & i) override
    { Read(i); return *this; }
    Archive & operator & (unsigned char & i) override
    { Read(i); return *this; }
    Archive & operator & (bool & b) override
    { Read(b); return *this; }
    Archive & operator & (std::string & str) override
    {
      int len;
      (*this) & len;
      str.resize(len);
      if(len)
        stream->read(&str[0], len); // NOLINT
      return *this;
    }
    Archive & operator & (char *& str) override
    {
      long len;
      (*this) & len;
      if(len == -1)
        str = nullptr;
      else
        {
          str = new char[len+1]; // NOLINT
          stream->read(&str[0], len); // NOLINT
          str[len] = '\0'; // NOLINT
        }
      return *this;
    }

    Archive & Do (double * d, size_t n) override
    { stream->read(reinterpret_cast<char*>(d), n*sizeof(double)); return *this; } // NOLINT
    Archive & Do (int * i, size_t n) override
    { stream->read(reinterpret_cast<char*>(i), n*sizeof(int)); return *this; } // NOLINT
    Archive & Do (size_t * i, size_t n) override
    { stream->read(reinterpret_cast<char*>(i), n*sizeof(size_t)); return *this; } // NOLINT

  private:
    template<typename T>
    inline void Read(T& val)
    { stream->read(reinterpret_cast<char*>(&val), sizeof(T)); } // NOLINT
  };

  // TextOutArchive ======================================================================
  class NGCORE_API TextOutArchive : public Archive
  {
  protected:
    std::shared_ptr<std::ostream> stream;
  public:
    TextOutArchive (std::shared_ptr<std::ostream>&& astream)
      : Archive(true), stream(std::move(astream))
    { }
    TextOutArchive (const std::string& filename) :
      TextOutArchive(std::make_shared<std::ofstream>(filename)) { }

    using Archive::operator&;
    Archive & operator & (double & d) override
    { *stream << d << '\n'; return *this; }
    Archive & operator & (int & i) override
    { *stream << i << '\n'; return *this; }
    Archive & operator & (short & i) override
    { *stream << i << '\n'; return *this; }
    Archive & operator & (long & i) override
    { *stream << i << '\n'; return *this; }
    Archive & operator & (size_t & i) override
    { *stream << i << '\n'; return *this; }
    Archive & operator & (unsigned char & i) override
    { *stream << int(i) << '\n'; return *this; }
    Archive & operator & (bool & b) override
    { *stream << (b ? 't' : 'f') << '\n'; return *this; }
    Archive & operator & (std::string & str) override
    {
      int len = str.length();
      *stream << len << '\n';
      if(len)
        {
          stream->write(&str[0], len); // NOLINT
          *stream << '\n';
        }
      return *this;
    }
    Archive & operator & (char *& str) override
    {
      long len = str ? strlen (str) : -1;
      *this & len;
      if(len > 0)
        {
          stream->write (&str[0], len); // NOLINT
          *stream << '\n';
        }
      return *this;
    }
  };

  // TextInArchive ======================================================================
  class NGCORE_API TextInArchive : public Archive
  {
  protected:
    std::shared_ptr<std::istream> stream;
  public:
    TextInArchive (std::shared_ptr<std::istream>&& astream) :
      Archive(false), stream(std::move(astream))
    { }
    TextInArchive (const std::string& filename)
      : TextInArchive(std::make_shared<std::ifstream>(filename)) {}

    using Archive::operator&;
    Archive & operator & (double & d) override
    { *stream >> d; return *this; }
    Archive & operator & (int & i) override
    { *stream >> i; return *this; }
    Archive & operator & (short & i) override
    { *stream >> i; return *this; }
    Archive & operator & (long & i) override
    { *stream >> i; return *this; }
    Archive & operator & (size_t & i) override
    { *stream >> i; return *this; }
    Archive & operator & (unsigned char & i) override
    { int _i; *stream >> _i; i = _i; return *this; }
    Archive & operator & (bool & b) override
    { char c; *stream >> c; b = (c=='t'); return *this; }
    Archive & operator & (std::string & str) override
    {
      int len;
      *stream >> len;
      char ch;
      stream->get(ch); // '\n'
      str.resize(len);
      if(len)
        stream->get(&str[0], len+1, '\0');
      return *this;
    }
    Archive & operator & (char *& str) override
    {
      long len;
      (*this) & len;
      char ch;
      if(len == -1)
        {
          str = nullptr;
          return (*this);
        }
      str = new char[len+1]; // NOLINT
      if(len)
        {
          stream->get(ch); // \n
          stream->get(&str[0], len+1, '\0'); // NOLINT
        }
      str[len] = '\0'; // NOLINT
      return *this;
    }
  };

#ifdef NETGEN_PYTHON

  template<typename ARCHIVE>
  class PyArchive : public ARCHIVE
  {
  private:
    pybind11::list lst;
    size_t index = 0;
  protected:
    using ARCHIVE::stream;
    using ARCHIVE::version_map;
    using ARCHIVE::logger;
    using ARCHIVE::GetLibraryVersions;
  public:
    PyArchive(const pybind11::object& alst = pybind11::none()) :
      ARCHIVE(std::make_shared<std::stringstream>()),
      lst(alst.is_none() ? pybind11::list() : pybind11::cast<pybind11::list>(alst))
    {
      ARCHIVE::shallow_to_python = true;
      if(Input())
        {
          stream = std::make_shared<std::stringstream>
            (pybind11::cast<pybind11::bytes>(lst[pybind11::len(lst)-1]));
          *this & version_map;
          stream = std::make_shared<std::stringstream>
            (pybind11::cast<pybind11::bytes>(lst[pybind11::len(lst)-2]));
        }
    }

    using ARCHIVE::Output;
    using ARCHIVE::Input;
    using ARCHIVE::FlushBuffer;
    using ARCHIVE::operator&;
    using ARCHIVE::operator<<;
    using ARCHIVE::GetVersion;
    void ShallowOutPython(const pybind11::object& val) override { lst.append(val); }
    pybind11::object ShallowInPython() override { return lst[index++]; }

    pybind11::list WriteOut()
    {
      FlushBuffer();
      lst.append(pybind11::bytes(std::static_pointer_cast<std::stringstream>(stream)->str()));
      stream = std::make_shared<std::stringstream>();
      *this & GetLibraryVersions();
      lst.append(pybind11::bytes(std::static_pointer_cast<std::stringstream>(stream)->str()));
      return lst;
    }
  };

  template<typename T, typename T_ARCHIVE_OUT=BinaryOutArchive, typename T_ARCHIVE_IN=BinaryInArchive>
  auto NGSPickle()
  {
    return pybind11::pickle([](T* self)
                      {
                        PyArchive<T_ARCHIVE_OUT> ar;
                        ar & self;
                        auto output = pybind11::make_tuple(ar.WriteOut());
                        NETGEN_DEBUG_LOG(GetLogger("Archive"), "pickling output for object of type " +
                                         Demangle(typeid(T).name()) + " = " +
                                         std::string(pybind11::str(output)));
                        return output;
                      },
                      [](pybind11::tuple state)
                      {
                        T* val = nullptr;
                        NETGEN_DEBUG_LOG(GetLogger("Archive"), "State for unpickling of object of type " +
                                         Demangle(typeid(T).name()) + " = " +
                                         std::string(pybind11::str(state[0])));
                        PyArchive<T_ARCHIVE_IN> ar(state[0]);
                        ar & val;
                        return val;
                      });
  }

#endif // NETGEN_PYTHON
} // namespace ngcore

#endif // NETGEN_CORE_ARCHIVE_HPP
