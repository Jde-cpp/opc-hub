#pragma once
#include "Group.h"

namespace Jde::Opc::Hist{
	//The host's `hist` block.  Both hosts read the same keys; only the path's default differs, hist/opc-gateway or
	//hist/opc-server under the host's logsDir, so the host passes it.
	struct Settings{
		Settings( fs::path path, SRCE )ε;
		Settings( const jobject& hist, fs::path defaultPath, SRCE )ε;
		fs::path Path;
		Duration Delay{ 1min };
		uint MaxBuffer{ 64*1024*1024 };//all groups' buffers together.
		//UTC unless an IANA name pins another, which must not change once Path holds files:  the day directories decide
		//which file a read opens.
		const std::chrono::time_zone* TimeZone;
		uint ReadLimit{ 10'000 };
	};

	//The library's root, one per host:  OpcServer adds its one group, `server`, at start; the gateway adds one per
	//hist_groups row, named by its guid, whenever one is created.
	struct Historian final : noncopyable{
		//Takes the exclusive lock on settings.Path.  When another process holds it, or the path can't be made, the host
		//runs without its historian, with a Critical log, rather than exiting.
		Historian( Settings settings, sp<IClock> clock )ι;
		//Writes what each group buffered, waiting for it, then drops the lock.  So the host ends its historian while the
		//process's executor still runs, where those writes complete, and from a thread that isn't the executor's.  Once the
		//executor is gone, what is buffered is lost, with an error.
		~Historian();
		//false for a host that runs without its historian:  it answers its history fields and /hist with an error, and
		//OpcServer installs no history backend.  AddGroup throws.
		α Enabled()Ι->bool;
		//members is the group's whole membership at start:  OpcServer's historized nodes, or a hist_groups row's
		//hist_group_nodes rows.  Add and Remove change it after.  What the group's files hold of it is restored first.
		α AddGroup( GroupConfig config, vector<Member> members={}, SRCE )ε->sp<Group>;
		//Deleting a hist_groups row:  every member leaves, by the caller who deleted it.  The group's files are kept, as
		//every archive is, and what it buffered is still written.
		α RemoveGroup( sv name, optional<Writer> by={}, SRCE )ε->void;
		α FindGroup( sv name )Ι->sp<Group>;
		α Config()Ι->const Settings&;
		α Time()Ι->IClock&;
		α Buffered()Ι->uint;//the memory every group's buffer takes together, which maxBuffer caps.
	private:
		const sp<Store> _store;
		mutable absl::Mutex _mutex;
		flat_map<string,sp<Group>,std::less<>> _groups ABSL_GUARDED_BY(_mutex);
		vector<sp<Group>> _removed ABSL_GUARDED_BY(_mutex);//each until a flush has written what it buffered.
	};
}