#pragma once
#include "process.h"

namespace Jde{
	//https://blog.benoitblanchon.fr/getprocaddress-like-a-boss/
	struct ProcPtr{
		ProcPtr( void* ptr ):
			_ptr(ptr)
		{}

		template<class T> requires std::is_function_v<T>
		operator T*() const{ return reinterpret_cast<T *>(_ptr); }
	private:
		void* _ptr;
	};
#undef LoadLibrary
	struct DllHelper{
		DllHelper( const fs::path& path )ε:
			_module{ Process::LoadLibrary(path) }
		{}
		DllHelper( const DllHelper& )=delete;
		α operator=( const DllHelper& )->DllHelper& =delete;

		~DllHelper(){
			Process::FreeLibrary( _module );
		}

		α operator[](str procName)Ε->ProcPtr{
			return ProcPtr( Process::GetProcAddress(_module, procName) );
		}
	private:
		void* _module;
	};

	//Process-wide cache of dll-backed api objects keyed by canonical path - the same dll requested twice, however spelled, shares one T.
	//Weak entries: T is destroyed (and its dll freed) when the last owner releases it; hold the sp to keep the api loaded.
	template<class T>
	struct DllApiCache{
		α Get( const fs::path& path )ε->sp<T>{
			std::error_code ec;
			auto key = fs::weakly_canonical( path, ec );
			if( ec )
				key = path; //unresolvable: as written, and let the load report.
			ul _{ _mutex };
			auto& cached = _apis[key];
			auto api = cached.lock();
			if( !api )
				cached = api = ms<T>( key );
			return api;
		}
	private:
		flat_map<fs::path,wp<T>> _apis ABSL_GUARDED_BY(_mutex);
		absl::Mutex _mutex;
	};
}