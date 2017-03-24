#pragma once
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/inotify.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <string.h>
#include <cstdio>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <map>
#include <list>
#include <set>
#include <queue>
#include <string>
#include <sstream>
#include <algorithm>

#ifdef WITH_ACTIVITY
  #include <activity.h>
#endif

#define MAX_EVENTS_PER_READ 1024

namespace inotifypp {

  //! Inotify monitor class
  class Monitor {
  private:
    /**
     * @brief Split string to tokens
     * @param Value String value to split
     * @param Tokens Returned tokens
     * @param Delimiters Delimiter chars
     */
    template <typename T>
    static void splitString(const std::string& Value, T& Tokens, const std::string& Delimiters = " ", bool TrimTokens = true, bool IgnoreEmpty = true) {
      // - Skip delimiters at begginig
      std::string::size_type LastPos = Value.find_first_not_of(Delimiters, 0);
      // - Find first non-delimiter
      std::string::size_type Pos = Value.find_first_of(Delimiters, LastPos);
      // - Split
      while (std::string::npos != Pos || std::string::npos != LastPos) {
        // - Found a toke, add to vector
        std::string Token = Value.substr(LastPos, Pos - LastPos);
        if (TrimTokens) trimInPlace(Token);
        if (!IgnoreEmpty || !Token.empty()) {
          Tokens.push_back(Token);
        }
        // - Skip delimiters
        LastPos = Value.find_first_not_of(Delimiters, Pos);
        // - Find next non-delimiter
        Pos = Value.find_first_of(Delimiters, LastPos);
      }
    }
    /**
     * @brief Trim left in place
     * @param String to trim
     */
    static inline void ltrimInPlace(std::string &s) {
      s.erase(s.begin(), std::find_if(s.begin(), s.end(), std::not1(std::ptr_fun<int, int>(std::isspace))));
    }

    /**
     * @brief Trim right in place
     * @param String to trim
     */
    static inline void rtrimInPlace(std::string &s) {
      s.erase(std::find_if(s.rbegin(), s.rend(), std::not1(std::ptr_fun<int, int>(std::isspace))).base(), s.end());
    }

    /**
     * @brief Trim left/right in place
     * @param String to trim
     */
    static inline void trimInPlace(std::string &s) {
      ltrimInPlace(s);
      rtrimInPlace(s);
    }

  public:

    //! Shared pointer to monitor instance
    std::shared_ptr<Monitor> ptr;

  public:

    //! Watcher class
    class Watch {
    friend class Monitor;
    public:

      //! Pointer to watch object
      typedef std::shared_ptr<Watch>  ptr;

      //! Constructor
      Watch(const std::string& name)
      : descriptor_(-1)
      , name_(name)
      , mask_(0)
      , internal_mask_(0)
      , parent_(nullptr)
      {}

      //! Get path to watch
      static std::string getWatchPath(ptr watch) {
        ptr item = watch;
        std::string path = item->getName();
        item = item->getParentWatch();
        while (item) {
          path = item->getName() + "/" + path;
          item = item->getParentWatch();
        }
        return path;
      }

      //! Get watch descriptor
      int getDescriptor() const {
        return descriptor_;
      }

      //! Get watch name
      const std::string& getName() const {
        return name_;
      }

      //! Get watch mask
      std::uint32_t getMask() const {
        return mask_;
      }

      //! Get watch internal mask
      std::uint32_t getInternalMask() const {
        return internal_mask_;
      }

      //! Get parent watch
      ptr getParentWatch() const {
        return parent_;
      }

      //! Get children watches
      std::map<std::string, ptr>& getChildrenWatches() {
        return children_;
      }

      //! Get child watch by name
      ptr getChildByName(const std::string& name) {
        auto item = getChildrenWatches().find(name);
        if (item == getChildrenWatches().end()) return nullptr;
        else return item->second;
      }

      //! Check is watch recursive
      bool isRecursive() {
        return recursive_;
      }

    private:

      //! Set watch parent
      void __setParent__(ptr parent) {
        parent_ = parent;
      }

      //! Set watch descriptor
      void __setDescriptor__(int descriptor) {
        descriptor_ = descriptor;
      }

      //! Set watch mask
      void __setMask__(std::uint32_t mask) {
        mask_ = mask;
      }

      //! Set watch internal mask
      void __setInternalMask__(std::uint32_t mask) {
        internal_mask_ = mask;
      }

      //! Set recursive mask
      void __setRecursive__(bool recursive) {
        recursive_ = recursive;
      }

      //! Set new name for watch
      void __setName__(const std::string& name) {
        name_ = name;
      }

    private:

      //! Watch descriptor
      int                         descriptor_;

      //! Path
      std::string                 name_;

      //! User mask
      std::uint32_t               mask_;

      //! Internal mask
      std::uint32_t               internal_mask_;

      //! Parent watch
      ptr                         parent_;

      //! Children watches
      std::map<std::string, ptr>  children_;

      //! Recursive
      bool                        recursive_;
    };

    //! Event class
    class Event {
    public:

      //! Pointer to event
      typedef std::shared_ptr<Event> ptr;

      //! Event type
      enum class Type {
        //! Object was created
        Created,
        //! Object was accessed (e.g. read(2), execve(2))
        Accessed,
        //! Object attribute changed
        AttributeChanged,
        //! Object opened for write was closed
        ClosedWrite,
        //! Object not opened for write wa closed
        ClosedNoWrite,
        //! Object was deleted
        Deleted,
        //! Object was modified (e.g. write(2), truncate(2))
        Modified,
        //! Object was opened
        Opened,
        //! Object was moved or renamed
        Moved
      };

    public:

      //! Constructor
      Event(Type event_type, const std::string& path, bool is_dirrectory)
      : type_(event_type)
      , path_(path)
      , is_dirrectory_(is_dirrectory)
      {}

      //! Constructor
      Event(Type event_type, const std::string& old_path, const std::string& new_path, bool is_dirrectory)
      : type_(event_type)
      , path_(new_path)
      , old_path_(old_path)
      , is_dirrectory_(is_dirrectory)
      {}

      //! Get event type
      Type getType() const {
        return type_;
      }

      //! Get path for event
      const std::string& getPath() const {
        return path_;
      }

      //! Get old path for [Moved] event
      const std::string& getOldPath() const {
        return old_path_;
      }

      //! Get folder path for event
      const std::string getFolderPath() const {
        return path_.substr(0, path_.find_last_of("/\\"));
      }

      //! Get file name for event
      const std::string getFileName() const {
        return path_.substr(path_.find_last_of("/\\") + 1);
      }

      //! Check is target was dirrectory
      const bool isDirrectory() const {
        return is_dirrectory_;
      }

    private:

      //! Event type
      Type          type_;

      //! Event path
      std::string   path_;

      //! Old event path for [Moved] event only
      std::string   old_path_;

      //! Is event on directory
      bool          is_dirrectory_;
    };


    //! Move event class
    class MoveEvent {
    friend class Monitor;
    public:

      //! Pointer to event
      typedef std::shared_ptr<MoveEvent> ptr;

    public:

      //! Constructor
      MoveEvent(int cookie, bool is_dir)
      : event_cookie_(cookie)
      , expire_at_(std::chrono::system_clock::now() + std::chrono::seconds(2))
      , is_dir_(is_dir)
      {}

      //! Get event cookie
      int getCookie() const {
        return event_cookie_;
      }

      //! Get object name for event
      const std::string& getNewName() const {
        return new_name_;
      }

      //! Get old object name for event
      const std::string& getOldName() const {
        return old_name_;
      }

      //! Get associated watch
      Watch::ptr getNewWatch() const {
        return new_watch_;
      }

      //! Get associated old watch
      Watch::ptr getOldWatch() const {
        return old_watch_;
      }

      //! Get is event for directory or not
      bool isDirrectory() const {
        return is_dir_;
      }

    private:

      //! Set object name for event
      void __setNewName__(const std::string& name) {
        new_name_ = name;
      }

      //! Set old object name for event
      void __setOldName__(const std::string& name) {
        old_name_ = name;
      }

      //! Set old watch object for event
      void __setOldWatch__(Watch::ptr watch) {
        old_watch_ = watch;
      }

      //! Set actual watch
      void __setNewWatch__(Watch::ptr watch) {
        new_watch_ = watch;
      }

      //! Get event timestamp
      const std::chrono::time_point<std::chrono::system_clock>& getExpireAt() const {
        return expire_at_;
      }

    private:

      //! Event cookie
      int                                                 event_cookie_;

      //! Name of event
      std::string                                         new_name_;

      //! Old name for move event
      std::string                                         old_name_;

      //! Associated watch
      Watch::ptr                                          new_watch_;

      //! Associated old watch for move event
      Watch::ptr                                          old_watch_;

      //! Pairing timeout
      std::chrono::time_point<std::chrono::system_clock>  expire_at_;

      //! Is dirrectory
      bool                                                is_dir_;
    };

  public:

    //! Constructor
    Monitor()
    : inotify_descriptor_(-1)
    , poll_descriptor_(-1)
    , watches_tree_(std::make_shared<Watch>("")) {
    }

    //! Destructor
    ~Monitor() {
      shutdown();
    }

    /**
     * @brief Init monitor
     * @return true on success
     */
    virtual bool init() {
      // - Check is already opened
      if (inotify_descriptor_ != -1) return false;

      // - Try to open inotify
      inotify_descriptor_ = inotify_init1(O_CLOEXEC | O_NONBLOCK);
      if (inotify_descriptor_ == -1) return false;

      // - Create polling
      poll_descriptor_ = epoll_create(sizeof(inotify_descriptor_));
      if (poll_descriptor_ == -1) {
        shutdown();
        return false;
      }

      // - Setup polling
      memset(&poll_event_, 0, sizeof(poll_event_));
      poll_event_.events = EPOLLIN | EPOLLPRI;
      if (epoll_ctl(poll_descriptor_, EPOLL_CTL_ADD, inotify_descriptor_, &poll_event_) == -1) {
        shutdown();
        return false;
      }

      // - Success
      return true;
    }

    /**
     * @brief Shutdown monitor
     */
    virtual void shutdown() {
      // - Close poling
      if (poll_descriptor_ != -1) {
        ::close(poll_descriptor_);
        poll_descriptor_ = -1;
      }
      // - Close inotify
      if (inotify_descriptor_ != -1) {
        ::close(inotify_descriptor_);
        inotify_descriptor_ = -1;
      }
    }

    /**
     * @brief Add directory watch
     * @param path Path to directory
     * @param mask Catch events mask
     * @param recursive Recursive monitoring
     * @return true on success
     */
    virtual bool addDirectoryWatch(const std::string& path, std::uint32_t mask, bool recursive) {
      // - Setup watch
      return addWatch(path, mask | IN_ONLYDIR, recursive);
    }


    /**
     * @brief Add single file watch
     * @param path Path to file
     * @param mask Catch events mask
     * @return true on success
     */
    virtual bool addFileWatch(const std::string& path, std::uint32_t mask) {
      return addWatch(path, mask, false);
    }

  private:

    //! Add watch
    virtual bool addWatch(const std::string& path, std::uint32_t mask, bool recursive) {
      printf("> ADD WATCH: %s\n", path.c_str());
      // - Create internal mask
      std::uint32_t internal_mask = mask
        | IN_DELETE_SELF
        | IN_MOVE_SELF
        | (recursive ? (IN_CREATE | IN_DELETE | IN_MOVE | IN_ONLYDIR) : 0)
      ;

      // - Get real path for source path
      char* real_path = realpath(path.c_str(), 0);
      if (!real_path) return false;
      std::string watch_path = real_path;
      free(real_path);

      // - Split path into tokens
      std::list<std::string> tokens;
      splitString(watch_path, tokens, "/");

      // - Create tree for watches
      Watch::ptr watch = watches_tree_;
      { std::lock_guard<std::mutex> lock(watches_mutex_);
        for (auto& name : tokens) {
          auto item = watch->getChildrenWatches().find(name);
          if (item != watch->getChildrenWatches().end()) {
            // - Go to next item
            watch = item->second;
          } else {
            // - Create & setup new item
            Watch::ptr new_item = std::make_shared<Watch>(name);
            new_item->__setParent__(watch);

            // - Apply recursive mask
            if (watch->getInternalMask() && watch->isRecursive()) {
              new_item->__setRecursive__(true);
              new_item->__setMask__(watch->getMask());
              new_item->__setInternalMask__(watch->getInternalMask());
            }

            // - Register & go next item
            watch->getChildrenWatches()[name] = new_item;
            watch = new_item;
          }
        }
      }

      // - Check is watch already registered
      if (watch->getDescriptor() != -1) return false;

      // - Initialize inotify watch
      int descriptor = inotify_add_watch(inotify_descriptor_, watch_path.c_str(), internal_mask);
      if (descriptor == -1) return false;

      // - Initialize
      watch->__setDescriptor__(descriptor);
      watch->__setMask__(mask);
      watch->__setInternalMask__(internal_mask);
      watch->__setRecursive__(recursive);

      // - Register watch as active
      { std::lock_guard<std::mutex> lock(watches_mutex_);
        watches_[descriptor] = watch;
      }

      // - Install watches to subdirectories
      if (recursive) {
        DIR* dir = opendir(watch_path.c_str());
        if (dir) {
          dirent64* entry = nullptr;
          while ((entry = readdir64(dir))) {
            // - Skip "." & ".."
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;

            // - Get full path
            std::string dir_path = watch_path + "/" + entry->d_name;

            // - Check is folder
            struct stat dir_stat;
            stat(dir_path.c_str(), &dir_stat);
            if (!S_ISDIR(dir_stat.st_mode)) continue;

            // - Install watch
            addWatch(dir_path, mask, recursive);
          }
          closedir(dir);
        }
      }
      return true;
    }

  public:

    /**
     * @brief Remove watch by path
     * @param path Path fo file or folder
     * @param recursive Recursive removal (for directory watch)
     * @return true on success
     */
    virtual bool removeWatch(const std::string& path, bool recursive) {
      printf("> REMOVE WATCH: %s\n", path.c_str());
      // - Get real path for source path
      char* real_path = realpath(path.c_str(), 0);
      std::string watch_path;
      if (real_path) {
        watch_path = real_path;
        free(real_path);
      } else watch_path = path;

      // - Split path into tokens
      std::list<std::string> tokens;
      splitString(watch_path, tokens, "/");

      // - Find watch
      Watch::ptr watch = watches_tree_;
      { std::lock_guard<std::mutex> lock(watches_mutex_);
        for (auto& name : tokens) {
          auto item = watch->getChildrenWatches().find(name);
          if (item != watch->getChildrenWatches().end()) {
            // - Go to next item
            watch = item->second;
          } else {
            // - Not found
            watch = nullptr;
            return false;
          }
        }
      }

      // - Remove watch
      inotify_rm_watch(inotify_descriptor_, watch->getDescriptor());

      // - Unregister watch from active
      { std::lock_guard<std::mutex> lock(watches_mutex_);
        watches_.erase(watch->getDescriptor());
      }

      // - Release watch information
      watch->__setDescriptor__(-1);

      // - Remove children nodes if recursive mode
      if (!watch->getChildrenWatches().empty() && recursive) {
        // - Recursive gather watch ids to remove
        static void (*gatherChildren)(Watch::ptr, std::set<int>&) = [](Watch::ptr object, std::set<int>& items)->void {
          // - Gather children
          for (auto& child : object->getChildrenWatches()) {
            items.insert(child.second->getDescriptor());
            gatherChildren(child.second, items);
          }
          // - Unlink from parent
          object->__setParent__(nullptr);
          // - Remove children
          object->getChildrenWatches().clear();
        };

        // - Just collect & remove watches
        std::set<int> kill_list;
        gatherChildren(watch, kill_list);
        for (auto& kill_id : kill_list) inotify_rm_watch(inotify_descriptor_, kill_id);
      }

      // - Unregister from parent
      if (watch->getChildrenWatches().empty() && watch->getParentWatch()) {
        watch->getParentWatch()->getChildrenWatches().erase(watch->getName());
        watch->__setParent__(nullptr);
      }
      return true;
    }

    /**
     * @brief Update monitoring
     */
    void update() {
      // - Buffer for inotify events (up to MAX_EVENTS_PER_READ events)
      char  buffer[sizeof(inotify_event) * MAX_EVENTS_PER_READ] __attribute__ ((aligned(__alignof__(struct inotify_event))));
      const inotify_event* event = nullptr;

      // - Poll inotify desctiptor
      int poll_result = epoll_wait(poll_descriptor_, &poll_event_, 1, 0);
      if (poll_result == -1) return;
      if (poll_result == 0) {
        // - Check pending events
        std::chrono::time_point<std::chrono::system_clock> current_timestamp = std::chrono::system_clock::now();
        std::set<int> processed_events;
        for (auto& i_event : pending_events_) {
          if (i_event.second->getExpireAt() > current_timestamp) continue;
          // - Reinterpret move action
          MoveEvent::ptr m_event = i_event.second;

          // - Event has old watch and don't have new watch -> Moving out of scope
          if (m_event->getOldWatch() && !m_event->getNewWatch()) {
            std::string remove_path;
            // - File was moved out from scope. Assume delete.
            auto child = m_event->getOldWatch()->getChildrenWatches().find(m_event->getOldName());
            if (child != m_event->getOldWatch()->getChildrenWatches().end()) {
              remove_path = Watch::getWatchPath(child->second);
              removeWatch(remove_path, child->second->isRecursive());
            } else {
              remove_path = Watch::getWatchPath(m_event->getOldWatch()) + "/" + m_event->getOldName();
            }
            // - Fire DELETE event
            { std::lock_guard<std::recursive_mutex> lock(outgoing_events_mutex_);
              outgoing_events_.push(std::make_shared<Event>(Event::Type::Deleted, remove_path, m_event->isDirrectory() ));
            }

          // - Event has new watch and don't have old watch -> Moving into scope
          } else if(!m_event->getOldWatch() && m_event->getNewWatch()) {
            // - File was move into scope. Assume create.
            std::string create_path = Watch::getWatchPath(m_event->getNewWatch()) + "/" + m_event->getNewName();
            struct stat new_stat;
            stat(create_path.c_str(), &new_stat);
            if (S_ISDIR(new_stat.st_mode)) {
              addWatch(create_path, m_event->getNewWatch()->getMask(), m_event->getNewWatch()->isRecursive());
            }
            // - Fire CREATE event
            { std::lock_guard<std::recursive_mutex> lock(outgoing_events_mutex_);
              outgoing_events_.push(std::make_shared<Event>(Event::Type::Created, create_path, m_event->isDirrectory()));
            }
          }

          // - Mark as processed
          processed_events.insert(m_event->getCookie());
        }
        for (auto& i_event : processed_events) {
          pending_events_.erase(i_event);
        }
        return;
      }

      // - Read inotify events
      std::size_t bytes_read = ::read(inotify_descriptor_, buffer, sizeof(buffer));
      if (bytes_read < 0) return;

      // - Process readed buffers
      for (char* pointer = buffer; pointer < buffer + bytes_read; pointer += sizeof(inotify_event) + event->len) {
        // - Setup event
        event = reinterpret_cast<const inotify_event*>(pointer);

        // - Find associated watch
        Watch::ptr watch = nullptr;
        { std::lock_guard<std::mutex> lock(watches_mutex_);
          auto i_watch = watches_.end();
          i_watch = watches_.find(event->wd);
          if (i_watch == watches_.end()) continue;
          watch = i_watch->second;
        }

        // - Get event name
        std::string event_name;
        if (event->len) event_name = event->name;

        // - Get event path
        std::string event_path = Watch::getWatchPath(watch) + "/" + event_name;

        // - Get watch internal mask
        std::uint32_t internal_mask = watch->getInternalMask();

        // - Get user mask
        std::uint32_t user_mask = watch->getMask();

        // - Process ignored
        if (event->mask & IN_IGNORED) {
          // - Unregister from active
          { std::lock_guard<std::mutex> lock(watches_mutex_);
            auto item = watches_.find(event->wd);
            if (item != watches_.end()) {
              item->second->__setDescriptor__(-1);
              watches_.erase(item);
            }
          }
          // - Stop processing
          return;
        }

        // -----------------------------------------------------------------------------
        // --- CREATE
        // -----------------------------------------------------------------------------
        if (event->mask & IN_CREATE && internal_mask & IN_CREATE) {
          // - Add new watch if recursive monitoring enabled
          if (event->mask & IN_ISDIR && watch->isRecursive()) {
            addWatch(event_path, watch->getInternalMask(), watch->isRecursive());
          }

        // -----------------------------------------------------------------------------
        // --- DELETE / SELF
        // -----------------------------------------------------------------------------
        } else if (event->mask & IN_DELETE_SELF && internal_mask & IN_DELETE_SELF) {
          // - Watcher was removed
          removeWatch(Watch::getWatchPath(watch), (event->mask & IN_ISDIR));

        // -----------------------------------------------------------------------------
        // --- MOVE / FROM
        // -----------------------------------------------------------------------------
        } else if (event->mask & IN_MOVED_FROM && internal_mask & IN_MOVED_FROM) {
          // - Find pending event
          MoveEvent::ptr pending_event = nullptr;
          { std::lock_guard<std::mutex> lock(pending_events_mutex_);
            auto i_pending = pending_events_.find(event->cookie);
            if (i_pending != pending_events_.end()) {
              pending_event = i_pending->second;
              pending_events_.erase(i_pending);
            }
          }

          // - Check is event exist
          if (!pending_event) {
            // - Create new event
            pending_event = std::make_shared<MoveEvent>(event->cookie, ((event->mask & IN_ISDIR) != 0));
            pending_event->__setOldName__(event_name);
            pending_event->__setOldWatch__(watch);
            { std::lock_guard<std::mutex> lock(pending_events_mutex_);
              pending_events_[event->cookie] = pending_event;
            }
          } else {
            pending_event->__setOldName__(event_name);
            pending_event->__setOldWatch__(watch);

            // - Find moved object
            std::string old_path = Watch::getWatchPath(pending_event->getOldWatch()) + "/" + pending_event->getOldName();
            std::string new_path;
            auto i_child = pending_event->getOldWatch()->getChildrenWatches().find(pending_event->getOldName());
            if (i_child != pending_event->getOldWatch()->getChildrenWatches().end()) {
              Watch::ptr object = i_child->second;

              // - Detach from old watch
              pending_event->getOldWatch()->getChildrenWatches().erase(i_child);

              // - Attach to new watch
              pending_event->getNewWatch()->getChildrenWatches()[pending_event->getNewName()] = object;
              object->__setParent__(pending_event->getNewWatch());

              // - Fix name
              object->__setName__(pending_event->getNewName());

              new_path = Watch::getWatchPath(object);
              if (user_mask & IN_MOVE) {
                std::lock_guard<std::recursive_mutex> lock(outgoing_events_mutex_);
                outgoing_events_.push(std::make_shared<Event>(Event::Type::Moved, old_path, new_path, ((event->mask & IN_ISDIR) != 0)));
              }
            } else {
              new_path = Watch::getWatchPath(pending_event->getNewWatch()) + "/" + pending_event->getNewName();
              if (user_mask & IN_MOVE) {
                std::lock_guard<std::recursive_mutex> lock(outgoing_events_mutex_);
                outgoing_events_.push(std::make_shared<Event>(Event::Type::Moved, old_path, new_path, ((event->mask & IN_ISDIR) != 0)));
              }
            }
          }

        // -----------------------------------------------------------------------------
        // --- MOVE / TO
        // -----------------------------------------------------------------------------
        } else if (event->mask & IN_MOVED_TO && internal_mask & IN_MOVED_TO) {
          // - Find pending event
          MoveEvent::ptr pending_event = nullptr;
          { std::lock_guard<std::mutex> lock(pending_events_mutex_);
            auto i_pending = pending_events_.find(event->cookie);
            if (i_pending != pending_events_.end()) {
              pending_event = i_pending->second;
              pending_events_.erase(i_pending);
            }
          }

          // - Check is event exist
          if (!pending_event) {
            // - Create new even
            pending_event = std::make_shared<MoveEvent>(event->cookie, ((event->mask & IN_ISDIR) != 0));
            pending_event->__setNewName__(event_name);
            pending_event->__setNewWatch__(watch);
            { std::lock_guard<std::mutex> lock(pending_events_mutex_);
              pending_events_[event->cookie] = pending_event;
            }
          } else {
            // - Update watch
            pending_event->__setNewName__(event_name);
            pending_event->__setNewWatch__(watch);

            // - Find moved object
            std::string old_path = Watch::getWatchPath(pending_event->getOldWatch()) + "/" + pending_event->getOldName();
            std::string new_path;
            auto i_child = pending_event->getOldWatch()->getChildrenWatches().find(pending_event->getOldName());
            if (i_child != pending_event->getOldWatch()->getChildrenWatches().end()) {
              Watch::ptr object = i_child->second;

              // - Detach from old watch
              pending_event->getOldWatch()->getChildrenWatches().erase(i_child);

              // - Attach to new watch
              pending_event->getNewWatch()->getChildrenWatches()[pending_event->getNewName()] = object;
              object->__setParent__(pending_event->getNewWatch());

              // - Fix name
              object->__setName__(pending_event->getNewName());

              new_path = Watch::getWatchPath(object);
              if (user_mask & IN_MOVE) {
                std::lock_guard<std::recursive_mutex> lock(outgoing_events_mutex_);
                outgoing_events_.push(std::make_shared<Event>(Event::Type::Moved, old_path, new_path, ((event->mask & IN_ISDIR) != 0)));
              }
            } else {
              new_path = Watch::getWatchPath(pending_event->getNewWatch()) + "/" + pending_event->getNewName();
              if (user_mask & IN_MOVE) {
                std::lock_guard<std::recursive_mutex> lock(outgoing_events_mutex_);
                outgoing_events_.push(std::make_shared<Event>(Event::Type::Moved, old_path, new_path, ((event->mask & IN_ISDIR) != 0)));
              }
            }
          }
        }

        // *** Process user events ***

        // -----------------------------------------------------------------------------
        // --- CREATE
        // -----------------------------------------------------------------------------
        if (event->mask & IN_CREATE && user_mask & IN_CREATE) {
          std::lock_guard<std::recursive_mutex> lock(outgoing_events_mutex_);
          outgoing_events_.push(std::make_shared<Event>(Event::Type::Created, event_path, ((event->mask & IN_ISDIR) != 0)));
        // -----------------------------------------------------------------------------
        // --- DELETE
        // -----------------------------------------------------------------------------
        } else if (event->mask & IN_DELETE && user_mask & IN_DELETE) {
          std::lock_guard<std::recursive_mutex> lock(outgoing_events_mutex_);
          outgoing_events_.push(std::make_shared<Event>(Event::Type::Deleted, event_path, ((event->mask & IN_ISDIR) != 0)));
        // -----------------------------------------------------------------------------
        // --- OPEN
        // -----------------------------------------------------------------------------
        } else if (event->mask & IN_OPEN && user_mask & IN_OPEN) {
          std::lock_guard<std::recursive_mutex> lock(outgoing_events_mutex_);
          outgoing_events_.push(std::make_shared<Event>(Event::Type::Opened, event_path, ((event->mask & IN_ISDIR) != 0)));
        // -----------------------------------------------------------------------------
        // --- ACCESS
        // -----------------------------------------------------------------------------
        } else if (event->mask & IN_ACCESS && user_mask & IN_ACCESS) {
          std::lock_guard<std::recursive_mutex> lock(outgoing_events_mutex_);
          outgoing_events_.push(std::make_shared<Event>(Event::Type::Accessed, event_path, ((event->mask & IN_ISDIR) != 0)));

        // -----------------------------------------------------------------------------
        // --- ATTRIB
        // -----------------------------------------------------------------------------
        } else if (event->mask & IN_ATTRIB && user_mask & IN_ATTRIB) {
          std::lock_guard<std::recursive_mutex> lock(outgoing_events_mutex_);
          outgoing_events_.push(std::make_shared<Event>(Event::Type::AttributeChanged, event_path, ((event->mask & IN_ISDIR) != 0)));

        // -----------------------------------------------------------------------------
        // --- CLOSE / WRITE
        // -----------------------------------------------------------------------------
        } else if (event->mask & IN_CLOSE_WRITE && user_mask & IN_CLOSE_WRITE) {
          std::lock_guard<std::recursive_mutex> lock(outgoing_events_mutex_);
          outgoing_events_.push(std::make_shared<Event>(Event::Type::ClosedWrite, event_path, ((event->mask & IN_ISDIR) != 0)));

        // -----------------------------------------------------------------------------
        // --- CLOSE / NO WRITE
        // -----------------------------------------------------------------------------
        } else if (event->mask & IN_CLOSE_NOWRITE && user_mask & IN_CLOSE_NOWRITE) {
          std::lock_guard<std::recursive_mutex> lock(outgoing_events_mutex_);
          outgoing_events_.push(std::make_shared<Event>(Event::Type::ClosedNoWrite, event_path, ((event->mask & IN_ISDIR) != 0)));

        // -----------------------------------------------------------------------------
        // --- MODIFY
        // -----------------------------------------------------------------------------
        } else if (event->mask & IN_MODIFY && user_mask & IN_MODIFY) {
          std::lock_guard<std::recursive_mutex> lock(outgoing_events_mutex_);
          outgoing_events_.push(std::make_shared<Event>(Event::Type::Modified, event_path, ((event->mask & IN_ISDIR) != 0)));
        }
      }
    }

    /**
     * @brief Get next event from queue
     * @return Pointer to event or nullptr if queue is empty
     */
    virtual Event::ptr getNextEvent() {
      std::lock_guard<std::recursive_mutex> lock(outgoing_events_mutex_);
      if (outgoing_events_.empty()) return nullptr;
      Event::ptr event = outgoing_events_.front();
      outgoing_events_.pop();
      return event;
    }

  private:

    //! Inotify descriptor
    int                                                 inotify_descriptor_;

    //! Polling descriptor
    int                                                 poll_descriptor_;

    //! Polling event object
    epoll_event                                         poll_event_;

    //! Watches collection
    std::map<int, Watch::ptr>                           watches_;

    //! Watches tree
    Watch::ptr                                          watches_tree_;

    //! Watches collection lock
    std::mutex                                          watches_mutex_;

    //! Pendind events by cookie for move
    std::unordered_map<std::uint32_t, MoveEvent::ptr>   pending_events_;

    //! Pending events mutex
    std::mutex                                          pending_events_mutex_;

    //! Outgoing events
    std::queue<Event::ptr>                              outgoing_events_;

    //! Outgoing events mutex
    std::recursive_mutex                                outgoing_events_mutex_;
  };


#ifdef WITH_ACTIVITY
  //! Active monitor
  class ActiveMonitor : private Monitor {
  public:
    //! Constructor
    ActiveMonitor()
    : monitor_activity_(this, &ActiveMonitor::__mointorActivity__)
    {}

    /**
     * @brief Init monitor
     * @return true on success
     */
    bool init() override {
      return Monitor::init();
    }

    /**
     * @brief Shutdown monitor
     */
    void shutdown() override {
      Monitor::shutdown();
    }

    /**
     * @brief Start monitoring
     */
    void start() {
      monitor_activity_.start();
    }

    /**
     * @brief Stop monitoring
     */
    void stop() {
      monitor_activity_.stop();
    }

    /**
     * @brief Add directory watch
     * @param path Path to directory
     * @param mask Catch events mask
     * @param recursive Recursive monitoring
     * @return true on success
     */
    bool addDirectoryWatch(const std::string& path, std::uint32_t mask, bool recursive) override {
      return Monitor::addDirectoryWatch(path, mask, recursive);
    }

    /**
     * @brief Add single file watch
     * @param path Path to file
     * @param mask Catch events mask
     * @return true on success
     */
    bool addFileWatch(const std::string& path, std::uint32_t mask) override {
      return Monitor::addFileWatch(path, mask);
    }

    /**
     * @brief Remove watch by path
     * @param path Path fo file or folder
     * @param recursive Recursive removal (for directory watch)
     * @return true on success
     */
    bool removeWatch(const std::string& path, bool recursive) override {
      return Monitor::removeWatch(path, recursive);
    }

    /**
     * @brief Get next event from queue
     * @return Pointer to event or nullptr if queue is empty
     */
    Event::ptr getNextEvent() override {
      return Monitor::getNextEvent();
    }

  private:

    //! Monitoring activity
    void __mointorActivity__() {
      while (monitor_activity_.running()) {
        monitor_activity_.__cancel_point__();
        update();
      }
    }

  private:
    //! Monitor activity
    activity<ActiveMonitor>   monitor_activity_;
  };
#endif
}
