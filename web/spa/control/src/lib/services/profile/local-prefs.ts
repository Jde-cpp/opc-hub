import { Service, WritableSignal, effect, inject, signal } from '@angular/core';

//Per-browser preferences, never the server's - the navbar's breadcrumb switch and the tab and view a page was left on.  Each is
//`<owner>/<name>` in localStorage, as JSON.  What follows the user between browsers is ProfileStore's.
@Service()
export class LocalPrefs{
	get<T>( owner:string, name:string, defaultValue:T ):T{
		const item = localStorage.getItem( this.#key(owner, name) );
		return item==null ? defaultValue : JSON.parse( item ) as T;
	}
	//null removes it
	set<T>( owner:string, name:string, value:T|null ):void{
		const key = this.#key( owner, name );
		if( value==null )
			localStorage.removeItem( key );
		else
			localStorage.setItem( key, JSON.stringify(value) );
	}
	tabIndex( owner:string ):number{ return this.get( owner, "tabIndex", 0 ); }
	setTabIndex( owner:string, index:number ):void{ this.set( owner, "tabIndex", index || null ); }
	viewIndex( owner:string ):number{ return this.get( owner, "viewIndex", 0 ); }
	setViewIndex( owner:string, index:number ):void{ this.set( owner, "viewIndex", index || null ); }

	#key( owner:string, name:string ):string{ return `${owner}/${name}`; }
}

//A tab group's selected index, restored from LocalPrefs under `owner` and written back as it changes - bound
//`[(selectedIndex)]="tabIndex"`.  An effect does the writing, so this is called in an injection context:  a field
//initialiser or a constructor.
export function persistedTabIndex( owner:string ):WritableSignal<number>{
	const prefs = inject( LocalPrefs );
	const index = signal<number>( prefs.tabIndex(owner) );
	effect( ()=>prefs.setTabIndex(owner, index()) );
	return index;
}