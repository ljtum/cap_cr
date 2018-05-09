#include <base/component.h>
#include <base/log.h>

namespace Fiasco {
#include <l4/sys/kdebug.h>
}

Genode::size_t Component::stack_size() { return 64*1024; }

void Component::construct(Genode::Env &)
{
	Genode::log("Hello! I am hello_child.");
//	enter_kdebug("Inside hello_child");
}
