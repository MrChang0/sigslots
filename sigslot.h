#pragma once
#ifndef SIGSLOT_DEFAULT_MT_POLICY
#ifdef SIGSLOT_SINGLE_THREADED
#define SIGSLOT_DEFAULT_MT_POLICY single_threaded
#else
#define SIGSLOT_DEFAULT_MT_POLICY multi_threaded_local
#endif
#endif

#include <set>
#include <vector>
#include <functional>
#include <memory>
#include <algorithm>
#include <mutex>

namespace sigslot {
	class single_threaded {
	public:
		single_threaded(){ }
		virtual ~single_threaded(){ }
		virtual void lock(){ }
		virtual void unlock(){ }
	};

	// TODO:remove global mutex
	class multi_threaded_global{
	public:
		multi_threaded_global(){
			get_critsec();
		}

		multi_threaded_global(const multi_threaded_global &){}
		virtual ~multi_threaded_global(){}

		virtual void lock(){
			get_critsec().lock();
		}

		virtual void unlock(){
			get_critsec().unlock();
		}

	private:
		static std::mutex & get_critsec(){
			static std::mutex mutex;
			return mutex;
		}
	};

	class multi_threaded_local {
	public:
		multi_threaded_local(){
		}

		multi_threaded_local(const multi_threaded_local &){
		}

		virtual ~multi_threaded_local(){
		}

		virtual void lock(){
			mutex_.lock();
		}

		virtual void unlock(){
			mutex_.unlock();
		}

	private:
		std::mutex mutex_;
	};

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

	template <typename MtPolicy,typename ... Types>
	class connection_base {
	public:
		virtual ~connection_base() = default;
		virtual has_slots<MtPolicy> * getdest() const = 0;
		virtual void emit(Types ...) = 0;
		virtual std::shared_ptr<connection_base<MtPolicy,Types...>> clone() = 0;
		virtual std::shared_ptr<connection_base<MtPolicy,Types...>> duplicate(has_slots<MtPolicy> * pnewdest) = 0;
	};

	template <class DestType,class MtPolicy,typename ... Types>
	class connection : public connection_base<MtPolicy,Types...> {
	public:
		connection() : pobject_(nullptr){}

		connection(DestType * pobject,std::function<void(DestType *,Types ...)> pmemfun) : pobject_(pobject),
		                                                                                   pmemfun_(pmemfun){ }

		std::shared_ptr<connection_base<MtPolicy,Types...>> clone() override{
			return std::shared_ptr<connection_base<MtPolicy,Types...>>(new connection<DestType,MtPolicy,Types...>(*this));
		}

		std::shared_ptr<connection_base<MtPolicy,Types...>> duplicate(has_slots<MtPolicy> * pnewdest) override{
			return std::shared_ptr<connection_base<MtPolicy,Types...>>(
				new connection<DestType,MtPolicy,Types...>(static_cast<DestType *>(pnewdest), pmemfun_));
		}

		void emit(Types ...args) override{
			pmemfun_(pobject_, args...);
		}

		has_slots<MtPolicy> * getdest() const override{
			return pobject_;
		}

	private:
		DestType * pobject_;
		std::function<void(DestType *,Types ...)> pmemfun_;
	};

	template <typename DestType,typename MtPolicy>
	class connection0 : public connection_base0<MtPolicy> {
	public:
		connection0() : pobject_(nullptr){}

		connection0(DestType * pobject,std::function<void(DestType *)> pmemfun) : pobject_(pobject),
		                                                                          pmemfun_(pmemfun){ }

		std::shared_ptr<connection_base0<MtPolicy>> clone() override{
			return std::shared_ptr<connection_base0<MtPolicy>>(new connection0<DestType,MtPolicy>(*this));
		}

		std::shared_ptr<connection_base0<MtPolicy>> duplicate(has_slots<MtPolicy> * pnewdest) override{
			return std::shared_ptr<connection_base0<MtPolicy>>(
				new connection0<DestType,MtPolicy>(static_cast<DestType *>(pnewdest), pmemfun_));
		}

		void emit() override{
			pmemfun_(pobject_);
		}

		has_slots<MtPolicy> * getdest() const override{
			return pobject_;
		}

	private:
		DestType * pobject_;
		std::function<void(DestType *)> pmemfun_;
	};

	template <typename MtPolicy>
	class signal_base_base : public MtPolicy {
		friend has_slots<MtPolicy>;
	public:
		virtual void slot_disconnect(has_slots<MtPolicy> * pslot) = 0;
		virtual void slot_duplicate(const has_slots<MtPolicy> * poldslot,has_slots<MtPolicy> * pnewslot) = 0;
	protected:
		virtual void slot_disconnect_nolock(has_slots<MtPolicy> * pslot) = 0;
	};

	template <typename MtPolicy,typename ... Types>
	class signal_base : public signal_base_base<MtPolicy> {

	public:
		typedef std::vector<std::shared_ptr<connection_base<MtPolicy,Types...>>> connections_type;

		signal_base(){
		}

		signal_base(const signal_base & s) : signal_base_base<MtPolicy>(s){
			lock_block<MtPolicy> lock(const_cast<signal_base<MtPolicy,Types...> *>(&s));

			for(auto && conn : s.connected_slots_) {
				auto new_conn = conn->clone();
				new_conn->getdest()->signal_connect(this);
				connected_slots_.push_back(std::move(new_conn));
			}
		}

		~signal_base(){
			disconnect_all();
		}

		void disconnect_all(){
			lock_block<MtPolicy> lock(this);
			for(auto && connect : connected_slots_) {
				connect->getdest()->signal_disconnect(this);
			}

			connected_slots_.clear();
		}

		void disconnect(has_slots<MtPolicy> * pclass){
			lock_block<MtPolicy> lock(this);
			auto it = std::find_if(connected_slots_.begin(), connected_slots_.end(),
			                       [=](const std::shared_ptr<connection_base<MtPolicy,Types...>> & conn) ->bool
		                       {
			                       return conn->getdest() == pclass;
		                       });
			if(it != connected_slots_.end()) {
				connected_slots_.erase(it);
				pclass->signal_disconnect(this);
			}
		}

		void slot_disconnect(has_slots<MtPolicy> * pslot) override{
			lock_block<MtPolicy> lock(this);
			slot_disconnect_nolock(pslot);
		}

		void slot_duplicate(const has_slots<MtPolicy> * oldtarget,has_slots<MtPolicy> * newtarget) override{
			lock_block<MtPolicy> lock(this);
			for(auto && connect : connected_slots_) {
				if(connect->getdest() == oldtarget) {
					connected_slots_.push_back(connect->duplicate(newtarget));
				}
			}
		}

	protected:
		connections_type connected_slots_;

		void slot_disconnect_nolock(has_slots<MtPolicy> * pslot) override{
			std::vector<typename connections_type::iterator> tmp_delete;
			for (auto it = connected_slots_.begin(); it != connected_slots_.end(); ++it) {
				if ((*it)->getdest() == pslot) {
					tmp_delete.push_back(it);
				}
			}
			for (auto && i : tmp_delete) {
				connected_slots_.erase(i);
			}
		}
	};

	template <typename MtPolicy>
	class signal_base0 : public signal_base_base<MtPolicy> {
		friend 	has_slots<MtPolicy>;
	public:
		typedef std::vector<std::shared_ptr<connection_base0<MtPolicy>>> connections_type;
		signal_base0(){ }

		signal_base0(const signal_base0 & s) : signal_base_base<MtPolicy>(s){
			lock_block<MtPolicy> lock(const_cast<signal_base0<MtPolicy>*>(&s));

			for(auto && conn : s.connected_slots_) {
				auto new_conn = conn->clone();
				new_conn->getdest()->signal_connect(this);
				connected_slots_.push_back(std::move(new_conn));
			}
		}

		~signal_base0(){
			disconnect_all();
		}

		void disconnect_all(){
			lock_block<MtPolicy> lock(this);
			for(auto connect : connected_slots_) {
				connect->getdest()->signal_disconnect(this);
			}

			connected_slots_.clear();
		}

		void disconnect(has_slots<MtPolicy> * pclass){
			lock_block<MtPolicy> lock(this);
			auto it = std::find_if(connected_slots_.begin(), connected_slots_.end(),
			                       [=](const std::shared_ptr<connection_base0<MtPolicy>> & conn) ->bool
		                       {
			                       return conn->getdest() == pclass;
		                       });
			if(it != connected_slots_.end()) {
				connected_slots_.erase(it);
				pclass->signal_disconnect(this);
			}
		}

		 void slot_disconnect(has_slots<MtPolicy> * pslot) override{
			lock_block<MtPolicy> lock(this);
			slot_disconnect_nolock(pslot);
		}

		 void slot_duplicate(const has_slots<MtPolicy> * oldtarget,has_slots<MtPolicy> * newtarget) override{
			lock_block<MtPolicy> lock(this);
			for(auto && connect : connected_slots_) {
				if(connect->getdest() == oldtarget) {
					connected_slots_.push_back(connect->duplicate(newtarget));
				}
			}
		}

	protected:
		connections_type connected_slots_;

		void slot_disconnect_nolock(has_slots<MtPolicy> * pslot) override{
			std::vector<typename connections_type::iterator> tmp_delete;
			for (auto it = connected_slots_.begin(); it != connected_slots_.end(); ++it) {
				if ((*it)->getdest() == pslot) {
					tmp_delete.push_back(it);
				}
			}
			for (auto && i : tmp_delete) {
				connected_slots_.erase(i);
			}
		}
	};

	template <typename MtPolicy = SIGSLOT_DEFAULT_MT_POLICY,typename ... Types>
	class Signal : public signal_base<MtPolicy,Types...> {
	public:
		Signal(){ }
		explicit Signal(const Signal<MtPolicy,Types...> & s) : signal_base<MtPolicy,Types...>(s){ }

		template <class DestType>
		void connect(DestType * pclass,void (DestType::*pmemfun)(Types ...)){
			lock_block<MtPolicy> lock(this);
			std::function<void(DestType *,Types ...)> fun(pmemfun);
			signal_base<MtPolicy,Types...>::connected_slots_.emplace_back(new connection<DestType, MtPolicy, Types...>(pclass, fun));
			pclass->signal_connect(this);
		}

		void emit(Types ...args){
			lock_block<MtPolicy> lock(this);
			for(auto && conn : signal_base<MtPolicy,Types...>::connected_slots_) {
				conn->emit(args...);
			}
		}

		void operator()(Types ...args){
			this->emit(args...);
		}
	};

	template <class MtPolicy = SIGSLOT_DEFAULT_MT_POLICY>
	class Signal0 : public signal_base0<MtPolicy> {
	public:
		Signal0(){ }
		Signal0(const Signal0<MtPolicy> & s) : signal_base0<MtPolicy>(s){ }

		template <typename DestType>
		void connect(DestType * pclass,void (DestType::*pmemfun)()){
			lock_block<MtPolicy> lock(this);
			std::function<void(DestType *)> fun(pmemfun);
			signal_base0<MtPolicy>::connected_slots_.emplace_back(new connection0<DestType, MtPolicy>(pclass, fun));
			pclass->signal_connect(this);
		}

		void emit(){
			lock_block<MtPolicy> lock(this);
			for(auto && conn : signal_base0<MtPolicy>::connected_slots_) {
				conn->emit();
			}
		}

		void operator()(){
			this->emit();
		}
	};

	template <typename MtPolicy = SIGSLOT_DEFAULT_MT_POLICY>
	class has_slots : public MtPolicy {
		typedef std::set<signal_base_base<MtPolicy> *> sender_set;
		typedef typename sender_set::const_iterator const_iterator;
	public:
		has_slots() { }

		has_slots(const has_slots & hs) : MtPolicy(hs) {
			lock_block<MtPolicy> lock(const_cast<has_slots<MtPolicy> *>(&hs));
			senders_.insert(hs.senders_.begin(), hs.senders_.end());
		}

		virtual ~has_slots() {
			disconnect_all();
		}

		void disconnect_all() {
			lock_block<MtPolicy> lock(this);
			for (auto && sender : senders_) {
				sender->slot_disconnect(this);
			}

			senders_.clear();
		}

		void signal_connect(signal_base_base<MtPolicy> * sender) {
			lock_block<MtPolicy> lock(this);
			senders_.insert(sender);
		}

		void signal_disconnect(signal_base_base<MtPolicy> * sender) {
			lock_block<MtPolicy> lock(this);
			senders_.erase(sender);
		}
	private:
		sender_set senders_;
	};

	template <>
	class has_slots<multi_threaded_global> : public multi_threaded_global {
		typedef std::set<signal_base_base<multi_threaded_global> *> sender_set;
		typedef sender_set::const_iterator const_iterator;
	public:
		has_slots() { }

		has_slots(const has_slots & hs) : multi_threaded_global(hs) {
			lock_block<multi_threaded_global> lock(const_cast<has_slots<multi_threaded_global> *>(&hs));
			senders_.insert(hs.senders_.begin(), hs.senders_.end());
		}

		virtual ~has_slots() {
			disconnect_all();
		}

		void disconnect_all() {
			lock_block<multi_threaded_global> lock(this);
			for (auto && sender : senders_) {
				sender->slot_disconnect_nolock(this);
			}

			senders_.clear();
		}

		void signal_connect(signal_base_base<multi_threaded_global> * sender) {
			senders_.insert(sender);
		}

		void signal_disconnect(signal_base_base<multi_threaded_global> * sender) {
			senders_.erase(sender);
		}
	private:
		sender_set senders_;
	};
}
