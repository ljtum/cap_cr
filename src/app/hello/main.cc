#include <base/component.h>
#include <base/log.h>
#include <base/env.h>
#include <base/sleep.h>
#include <base/child.h>
#include <ram_session/connection.h>
#include <rom_session/connection.h>
#include <cpu_session/connection.h>
#include <cap_session/connection.h>
#include <pd_session/connection.h>
#include <loader_session/connection.h>
#include <region_map/client.h>
#include <timer_session/connection.h>

namespace Fiasco {
#include <l4/sys/kdebug.h>
}

/***************
 ** Utilities **
 ***************/

static void wait_for_signal_for_context(Genode::Signal_receiver &sig_rec,
                                        Genode::Signal_context const &sig_ctx)
{
	Genode::Signal s = sig_rec.wait_for_signal();

	if (s.num() && s.context() == &sig_ctx) {
		Genode::log("got exception for child");
	} else {
		Genode::error("got unexpected signal while waiting for child");
		class Unexpected_signal { };
		throw Unexpected_signal();
	}
}

class Hello_child_policy : public Genode::Child_policy
{
private:

	Genode::Parent_service    _log_service;
	Genode::Parent_service    _rm_service;

public:

	Hello_child_policy()
	: _log_service("LOG"), _rm_service("RM")
	{}

	/****************************
	 ** Child-policy interface **
	 ****************************/

	const char *name() const { return "hello_child"; }

	Genode::Service *resolve_session_request(const char *service, const char *)
	{
		/* forward white-listed session requests to our parent */
					return !Genode::strcmp(service, "LOG") ? &_log_service
					     : !Genode::strcmp(service, "RM")  ? &_rm_service
					     : 0;
	}

	void filter_session_args(const char *service, char *args, Genode::size_t args_len)
	{
		/* define session label for sessions forwarded to our parent */
		Genode::Arg_string::set_arg_string(args, args_len, "label", "hello_child");
	}
};

class Test_child : public Genode::Child_policy
{
	private:

		struct Resources
		{
			Genode::Pd_connection  pd;
			Genode::Ram_connection ram;
			Genode::Cpu_connection cpu;

			Resources(Genode::Signal_context_capability sigh, char const *label)
			: pd(label)
			{
				using namespace Genode;

				/* transfer some of our own ram quota to the new child */
				enum { CHILD_QUOTA = 1*1024*1024 };
				ram.ref_account(env()->ram_session_cap());
				env()->ram_session()->transfer_quota(ram.cap(), CHILD_QUOTA);

				/* register default exception handler */
				cpu.exception_sigh(sigh);

				/* register handler for unresolvable page faults */
				Region_map_client address_space(pd.address_space());
				address_space.fault_handler(sigh);
			}
		} _resources;

		Genode::Child::Initial_thread _initial_thread;

		/*
		 * The order of the following members is important. The services must
		 * appear before the child to ensure the correct order of destruction.
		 * I.e., the services must remain alive until the child has stopped
		 * executing. Otherwise, the child may hand out already destructed
		 * local services when dispatching an incoming session call.
		 */
		Genode::Rom_connection    _elf;
		Genode::Parent_service    _log_service;
		Genode::Parent_service    _rm_service;
		Genode::Region_map_client _address_space { _resources.pd.address_space() };
		Genode::Child             _child;

	public:

		/**
		 * Constructor
		 */
		Test_child(Genode::Rpc_entrypoint           &ep,
		           char const                       *elf_name,
		           Genode::Signal_context_capability sigh)
		:
			_resources(sigh, elf_name),
			_initial_thread(_resources.cpu, _resources.pd, elf_name),
			_elf(elf_name),
			_log_service("LOG"), _rm_service("RM"),
			_child(_elf.dataspace(), Genode::Dataspace_capability(),
			       _resources.pd,  _resources.pd,
			       _resources.ram, _resources.ram,
			       _resources.cpu, _initial_thread,
			       *Genode::env()->rm_session(), _address_space, ep, *this)
		{ }


		/****************************
		 ** Child-policy interface **
		 ****************************/

		const char *name() const { return "child"; }

		Genode::Service *resolve_session_request(const char *service, const char *)
		{
			/* forward white-listed session requests to our parent */
			return !Genode::strcmp(service, "LOG") ? &_log_service
			     : !Genode::strcmp(service, "RM")  ? &_rm_service
			     : 0;
		}

		void filter_session_args(const char *service,
		                         char *args, Genode::size_t args_len)
		{
			/* define session label for sessions forwarded to our parent */
			Genode::Arg_string::set_arg_string(args, args_len, "label", "child");
		}
};

Genode::size_t Component::stack_size() { return 64*1024; }

void Component::construct(Genode::Env &env)
{
	using namespace Genode;

	Timer::Connection timer { env };

	const char* elf_name = "hello_child";

	log("-------------------- Creating hello_child --------------------");
//	enter_kdebug("Before pd_connection");
	log("-------------------- Creating pd_connection --------------------");
	Genode::Pd_connection  pd(env, elf_name);
	log("-------------------- Pd_connection created --------------------");
//	enter_kdebug("After pd_connection");

	log("-------------------- Creating cpu_connection --------------------");
	Genode::Cpu_connection cpu;
	log("-------------------- Cpu_connection created --------------------");

	// transfer some of our own ram quota to the new child
	enum { CHILD_QUOTA = 1*1024*1024 };
	log("-------------------- Creating ram_connection --------------------");
	Genode::Ram_connection ram;
	log("-------------------- Ram_connection created --------------------");
/*	ram.ref_account(Genode::env()->ram_session_cap());
	Genode::env()->ram_session()->transfer_quota(ram.cap(), CHILD_QUOTA);
*/
	log("-------------------- Referencing RAM account --------------------");
	ram.ref_account(env.ram_session_cap());
	log("-------------------- RAM account referenced --------------------");
	log("-------------------- Transfering RAM quota --------------------");
	env.ram().transfer_quota(ram.cap(), CHILD_QUOTA);
	log("-------------------- Ram quota transferred --------------------");

	log("-------------------- Creating Initial_thread --------------------");
	Genode::Child::Initial_thread _initial_thread(cpu, pd, "hello_child thread");
	log("-------------------- Initial_thread created --------------------");

	log("-------------------- Creating rom_connection --------------------");
	Genode::Rom_connection    _elf(elf_name);
	log("-------------------- Rom_connection created --------------------");

	log("-------------------- Creating Region_map_client --------------------");
	Genode::Region_map_client _address_space( pd.address_space() );
	log("-------------------- Region_map_client created --------------------");

	enum { STACK_SIZE = 8*1024 };
	log("-------------------- Creating Cap_connection --------------------");
	Cap_connection cap;
	log("-------------------- Cap_connection created --------------------");

	log("-------------------- Creating Rpc_entrypoint --------------------");
	Rpc_entrypoint ep(&cap, STACK_SIZE, "hello_child ep");
	log("-------------------- Rpc_entrypoint created --------------------");

	log("-------------------- Creating Hello_child_policy --------------------");
	Hello_child_policy my_child_policy;
	log("-------------------- Hello_child_policy created --------------------");

	log("-------------------- Creating Child my_child --------------------");
//	enter_kdebug("Before Child");
	Child my_child(
					_elf.dataspace(), // valid dataspace that contains elf. loads and executes it.
				//	Genode::Dataspace_capability(), // invalid dataspace creates Child shell without loaded elf
					Genode::Dataspace_capability(), // normal second argument. not related to above.
					pd, pd,
					ram, ram,
					cpu, _initial_thread,
					*Genode::env()->rm_session(), _address_space,
					ep, my_child_policy);
	log("-------------------- Child my_child created --------------------");

	log("-------------------- Waiting 3 seconds --------------------");
	timer.msleep(3000);
	log("-------------------- Done! --------------------");

#if 0
	/*
	 * Entry point used for serving the parent interface
	 */
	enum { STACK_SIZE = 8*1024 };
	Cap_connection cap;
	Rpc_entrypoint ep(&cap, STACK_SIZE, "child");

	/*
	 * Signal receiver and signal context for signals originating from the
	 * children's CPU-session and RM session.
	 */
	Signal_receiver sig_rec;
	Signal_context  sig_ctx;

	log("create child");

	/* create and start child process */
	Test_child child(ep, "hello_child", sig_rec.manage(&sig_ctx));

//	log("wait_for_signal");
//	wait_for_signal_for_context(sig_rec, sig_ctx);
	sig_rec.dissolve(&sig_ctx);
#endif

	Genode::log("Hello cap_cr!");
}

