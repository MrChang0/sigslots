#pragma once
#if defined(SIGSLOT_PURE_ISO) || (!defined(WIN32) && !defined(__GNUG__) && !defined(SIGSLOT_USE_POSIX_THREADS))
#	define _SIGSLOT_SINGLE_THREADED
#elif defined(WIN32)
#	define _SIGSLOT_HAS_WIN32_THREADS
#	include <windows.h>
#elif defined(__GNUG__) || defined(SIGSLOT_USE_POSIX_THREADS)
#	define _SIGSLOT_HAS_POSIX_THREADS
#	include <pthread.h>
#else
#	define _SIGSLOT_SINGLE_THREADED
#endif
#ifndef SIGSLOT_DEFAULT_MT_POLICY
#	ifdef _SIGSLOT_SINGLE_THREADED
#		define SIGSLOT_DEFAULT_MT_POLICY single_threaded
#	else
#		define SIGSLOT_DEFAULT_MT_POLICY multi_threaded_local
#	endif
#endif
#include <set>
#include <vector>
#include <functional>
#include <memory>
#include <algorithm>
namespace sigslot {
	class single_threaded {
	public:
		single_threaded(){
		}

		virtual ~single_threaded(){
		}

		virtual void lock(){
		}

		virtual void unlock(){
		}
	};
#ifdef _SIGSLOT_HAS_WIN32_THREADS
	// The multi threading policies only get compiled in if they are enabled.
	class multi_threaded_global{
	public:
		multi_threaded_global(){
			static bool isinitialised = false;

			if (!isinitialised){
				InitializeCriticalSection(get_critsec());
				isinitialised = true;
			}
		}

		multi_threaded_global(const multi_threaded_global&){}

		virtual ~multi_threaded_global(){}

		virtual void lock(){
			EnterCriticalSection(get_critsec());
		}

		virtual void unlock(){
			LeaveCriticalSection(get_critsec());
		}

	private:
		CRITICAL_SECTION* get_critsec()
		{
			static CRITICAL_SECTION g_critsec;
			return &g_critsec;
		}
	};

	class multi_threaded_local{
	public:
		multi_threaded_local(){
			InitializeCriticalSection(&m_critsec);
		}

		multi_threaded_local(const multi_threaded_local&){
			InitializeCriticalSection(&m_critsec);
		}

		virtual ~multi_threaded_local(){
			DeleteCriticalSection(&m_critsec);
		}

		virtual void lock(){
			EnterCriticalSection(&m_critsec);
		}

		virtual void unlock(){
			LeaveCriticalSection(&m_critsec);
		}

	private:
		CRITICAL_SECTION m_critsec;
	};
#endif // _SIGSLOT_HAS_WIN32_THREADS
#ifdef _SIGSLOT_HAS_POSIX_THREADS
	// The multi threading policies only get compiled in if they are enabled.
	class multi_threaded_global{
	public:
		multi_threaded_global(){
			pthread_mutex_init(get_mutex(), NULL);
		}

		multi_threaded_global(const multi_threaded_global&){}

		virtual ~multi_threaded_global(){
		}

		virtual void lock(){
			pthread_mutex_lock(get_mutex());
		}

		virtual void unlock(){
			pthread_mutex_unlock(get_mutex());
		}

	private:
		pthread_mutex_t* get_mutex(){
			static pthread_mutex_t g_mutex;
			return &g_mutex;
		}
	};

	class multi_threaded_local{
	public:
		multi_threaded_local(){
			pthread_mutex_init(&m_mutex, NULL);
		}

		multi_threaded_local(const multi_threaded_local&){
			pthread_mutex_init(&m_mutex, NULL);
		}

		virtual ~multi_threaded_local(){
			pthread_mutex_destroy(&m_mutex);
		}

		virtual void lock(){
			pthread_mutex_lock(&m_mutex);
		}

		virtual void unlock(){
			pthread_mutex_unlock(&m_mutex);
		}

	private:
		pthread_mutex_t m_mutex;
	};
#endif // _SIGSLOT_HAS_POSIX_THREADS
	template <typename MtPolicy>
	class lock_block {
	public:
		MtPolicy * m_mutex;

		explicit lock_block(MtPolicy * mtx) : m_mutex(mtx){
			m_mutex->lock();
		}

		~lock_block(){
			m_mutex->unlock();
		}
	};

	template <typename MtPolicy>
	class has_slots;

	template <typename MtPolicy>
	class connection_base0 {
	public:
		virtual ~connection_base0() = default;
		virtual has_slots<MtPolicy> * getdest() const = 0;
		virtual void emit() = 0;
		virtual std::shared_ptr<connection_base0<MtPolicy>> clone() = 0;
		virtual std::shared_ptr<connection_base0<MtPolicy>> duplicate(has_slots<MtPolicy> * pnewdest) = 0;
	};

	template <typename MtPolicy, typename ... Types>
	class connection_base {
	public:
		virtual ~connection_base() = default;
		virtual has_slots<MtPolicy> * getdest() const = 0;
		virtual void emit(Types ...) = 0;
		virtual std::shared_ptr<connection_base<MtPolicy,Types...>> clone() = 0;
		virtual std::shared_ptr<connection_base<MtPolicy,Types...>> duplicate(has_slots<MtPolicy> * pnewdest) = 0;
	};

	template <typename MtPolicy>
	class signal_base_base : public MtPolicy {
	public:
		virtual void slot_disconnect(has_slots<MtPolicy> * pslot) = 0;
		virtual void slot_duplicate(const has_slots<MtPolicy> * poldslot,has_slots<MtPolicy> * pnewslot) = 0;
	};

	template <typename MtPolicy = SIGSLOT_DEFAULT_MT_POLICY>
	class has_slots : public MtPolicy {
	private:
		typedef std::set<signal_base_base<MtPolicy> *> sender_set;
		typedef typename sender_set::const_iterator const_iterator;
	public:
		has_slots(){
		}

		has_slots(const has_slots & hs) : MtPolicy(hs){
			sender_set tmp;
			{
				lock_block<MtPolicy> lock(hs);
				tmp.insert(hs.senders_.begin(), hs.senders_.end());
			}
			for(auto sender: tmp) {
				sender->slot_duplicate(&hs, this);
				senders_.insert(sender);
			}
		}

		void signal_connect(signal_base_base<MtPolicy> * sender){
			lock_block<MtPolicy> lock(this);
			senders_.insert(sender);
		}

		void signal_disconnect(signal_base_base<MtPolicy> * sender){
			lock_block<MtPolicy> lock(this);
			senders_.erase(sender);
		}

		virtual ~has_slots(){
			disconnect_all();
		}

		void disconnect_all(){
			lock_block<MtPolicy> lock(this);
			for(auto sender:senders_) {
				sender->slot_disconnect(this);
			}

			senders_.clear();
		}

	private:
		sender_set senders_;
	};

	template <typename MtPolicy>
	class signal_base0 : public signal_base_base<MtPolicy> {
	public:
		typedef std::vector<std::shared_ptr<connection_base0<MtPolicy>>> connections_type;

		signal_base0(){
			;
		}

		signal_base0(const signal_base0 & s) : signal_base_base<MtPolicy>(s){
			connections_type tmp;
			{
				lock_block<MtPolicy> lock(s);
				tmp.insert(s.connected_slots_.begin(), s.connected_slots_.end());
			}

			for(auto connect:tmp) {
				connect->getdest()->signal_connect(this);
				connected_slots_.push_back(connect->clone());
			}
		}

		~signal_base0(){
			disconnect_all();
		}

		void disconnect_all(){
			lock_block<MtPolicy> lock(this);
			for(auto connect:connected_slots_) {
				connect->getdest()->si gnal_disconnect(this);
			}

			connected_slots_.clear();
		}

		void disconnect(has_slots<MtPolicy> * pclass){
			lock_block<MtPolicy> lock(this);
			auto it = std::find_if(connected_slots_.begin(), connected_slots_.end(),
				[=](const std::shared_ptr<connection_base<MtPolicy>>& conn) -> bool {
				return conn->getdest() == pclass;
			});
			if (it != connected_slots_.end()) {
				connected_slots_.erase(it);
				pclass->signal_disconnect(this)
			}
		}

		virtual void slot_disconnect(has_slots<MtPolicy> * pslot) override{
			lock_block<MtPolicy> lock(this);
			vector<decltype(it)> tmp_delete;

			for (auto it = connected_slots_.begin(); it != connected_slots_.end(); ++it) {
				if ((*it)->getdest() == pslot) {
					tmp_delete.pop_back(it);
				}
			}
			for (auto && i : tmp_delete) {
				connected_slots_.erase(i);
			}
		}

		virtual void slot_duplicate(const has_slots<MtPolicy> * oldtarget,has_slots<MtPolicy> * newtarget) override{
			lock_block<MtPolicy> lock(this);
			auto it = connected_slots_.begin();
			auto it_end = connected_slots_.end();

			while(it != it_end) {
				if((*it)->getdest() == oldtarget) {
					connected_slots_.push_back((*it)->duplicate(newtarget));
				}

				++it;
			}
		}

	protected:
		connections_type connected_slots_;
	};

	template < typename MtPolicy,typename ... Types>
	class signal_base : public signal_base_base<MtPolicy> {
	public:
		typedef std::vector<std::shared_ptr<connection_base<MtPolicy,Types...>>> connections_type;

		signal_base() {
			;
		}

		signal_base(const signal_base & s) : signal_base_base<MtPolicy>(s) {
			connections_type tmp;
			{
				lock_block<MtPolicy> lock(s);
				tmp.insert(s.connected_slots_.begin(), s.connected_slots_.end());
			}
			for (auto && connect : tmp) {
				connect->getdest()->signal_connect(this);
				connected_slots_.push_back(connect->clone());
			}
		}

		~signal_base() {
			disconnect_all();
		}

		void disconnect_all() {
			lock_block<MtPolicy> lock(this);
			for (auto && connect : connected_slots_) {
				connect->getdest()->signal_disconnect(this);
			}

			connected_slots_.clear();
		}

		void disconnect(has_slots<MtPolicy> * pclass) {
			lock_block<MtPolicy> lock(this);
			auto it = std::find_if(connected_slots_.begin(),connected_slots_.end(),
				[=](const std::shared_ptr<connection_base<MtPolicy, Types...>>& conn) -> bool {
				return conn->getdest() == pclass;
			});
			if(it != connected_slots_.end()) {
				connected_slots_.erase(it);
				pclass->signal_disconnect(this);
			}
		}

		virtual void slot_disconnect(has_slots<MtPolicy> * pslot) override {
			lock_block<MtPolicy> lock(this);
			vector<decltype(it)> tmp_delete;
			for (auto it = connected_slots_.begin(); it != connected_slots_.end(); ++it) {
				if ((*it)->getdest() == pslot) {
					tmp_delete.pop_back(it);
				}
			}
			for (auto && i : tmp_delete) {
				connected_slots_.erase(i);
			}
		}

		virtual void slot_duplicate(const has_slots<MtPolicy> * oldtarget, has_slots<MtPolicy> * newtarget) override {
			lock_block<MtPolicy> lock(this);

			for (auto && connect : connected_slots_) {
				if (connect->getdest() == oldtarget) {
					connected_slots_.push_back(connect->duplicate(newtarget));
				}
			}
		}

	protected:
		connections_type connected_slots_;
	};

	template <typename DestType, typename MtPolicy>
	class connection0 : public connection_base0<MtPolicy> {
	public:
		connection0() : pobject_(nullptr){}

		connection0(DestType * pobject,std::function<void(DestType *)> pmemfun) : pobject_(pobject),
		                                                                               pmemfun_(pmemfun){ }

		std::shared_ptr<connection_base0<MtPolicy>> clone() override{
			return std::shared_ptr<connection_base0<MtPolicy>>(new connection0<DestType,MtPolicy>(*this));
		}

		std::shared_ptr<connection_base0<MtPolicy>> duplicate(has_slots<MtPolicy> * pnewdest) override{
			return std::shared_ptr<connection_base0<MtPolicy>>(new connection0<DestType,MtPolicy>(static_cast<DestType *>(pnewdest), pmemfun_));
		}

		virtual void emit() override{
			pmemfun_(pobject_);
		}

		virtual has_slots<MtPolicy> * getdest() const override{
			return pobject_;
		}

	private:
		DestType * pobject_;
		std::function<void(DestType *)> pmemfun_;
	};

	template <class DestType,class MtPolicy,typename ... Types>
	class connection : public connection_base<MtPolicy,Types...> {
	public:
		connection(): pobject_(nullptr){}

		connection(DestType * pobject,std::function<void(DestType *,Types ...)> pmemfun): pobject_(pobject),
		                                                                                        pmemfun_(pmemfun){ }

		std::shared_ptr<connection_base<MtPolicy,Types...>> clone() override{
			return std::shared_ptr<connection_base<MtPolicy, Types...>>(new connection<DestType, MtPolicy,Types...>(*this));
		}

		std::shared_ptr<connection_base<MtPolicy,Types...>> duplicate(has_slots<MtPolicy> * pnewdest) override{
			return std::shared_ptr<connection_base<MtPolicy, Types...>>(new connection<DestType, MtPolicy,Types...>(static_cast<DestType *>(pnewdest), pmemfun_));
		}
		
		virtual void emit(Types ...args) override{
			pmemfun_(pobject_, args...);
		}

		has_slots<MtPolicy> * getdest() const override{
			return pobject_;
		}

	private:
		DestType * pobject_;
		std::function<void(DestType *,Types ...)> pmemfun_;
	};

	template <class MtPolicy = SIGSLOT_DEFAULT_MT_POLICY>
	class signal0 : public signal_base0<MtPolicy> {
	public:
		signal0(){ }
		signal0(const signal0<MtPolicy> & s) : signal_base0<MtPolicy>(s){ }

		template <typename DestType>
		void connect(DestType * pclass, void (DestType::*pmemfun)()){
			lock_block<MtPolicy> lock(this);
			std::function<void(DestType *)> fun(pmemfun);
			std::shared_ptr<connection_base0<MtPolicy>> conn(new connection0<DestType,MtPolicy>(pclass, fun));
			signal_base0<MtPolicy>::connected_slots_.push_back(conn);
			pclass->signal_connect(this);
		}

		void emit(){
			lock_block<MtPolicy> lock(this);
			for(auto conn : signal_base0<MtPolicy>::connected_slots_) {
				conn->emit();
			}
		}

		void operator()(){
			this->emit();
		}
	};

	template <typename MtPolicy = SIGSLOT_DEFAULT_MT_POLICY,typename ... Types>
	class signal : public signal_base<MtPolicy,Types...> {
	public:
		signal() { }
		explicit signal(const signal<MtPolicy,Types...> & s) : signal_base<MtPolicy,Types...>(s) { }

		template <class DestType>
		void connect(DestType * pclass, void (DestType::*pmemfun)(Types...)) {
			lock_block<MtPolicy> lock(this);
			std::function<void(DestType *, Types ...)> fun(pmemfun);
			std::shared_ptr<connection_base<MtPolicy,Types...>> conn(new connection<DestType, MtPolicy,Types...>(pclass, fun));
			signal_base<MtPolicy,Types...>::connected_slots_.push_back(conn);
			pclass->signal_connect(this);
		}

		void emit(Types...args) {
			lock_block<MtPolicy> lock(this);
			for (auto connection : signal_base<MtPolicy,Types...>::connected_slots_) {
				connection->emit(args...);
			}
		}

		void operator()() {
			this->emit();
		}
	};
}
