import { signal } from '@angular/core';
import { Latest } from 'jde-spa';
import { errorMessage } from '../../../utils/errors';

//A tab's rows on the calendar's page (#396) - its patterns, a pattern's shifts, its exceptions:  the rows, a read or an edit in
//flight, and the application server's refusal of either, which the tab says in place.
export class CalendarRows<T>{
	rows = signal<T[]>( [] );
	loading = signal( false );
	busy = signal( false );
	error = signal<string|undefined>( undefined );
	#reads = new Latest();

	reset():void{
		this.rows.set( [] );
		this.error.set( undefined );
	}
	//`read` answers what the tab shows, and `land` takes it only while it is the last read asked for and `current()` holds:  the
	//router reuses the page for another calendar meanwhile.
	async read<R>( read:()=>Promise<R>, land:( answer:R )=>void, current:()=>boolean ):Promise<void>{
		const ticket = this.#reads.next();
		this.loading.set( true );
		this.error.set( undefined );
		try{
			const answer = await read();
			if( this.#reads.is( ticket ) && current() )
				land( answer );
		}
		catch( e ){
			if( this.#reads.is( ticket ) && current() )
				this.error.set( errorMessage(e) );
		}
		finally{
			if( this.#reads.is( ticket ) )
				this.loading.set( false );
		}
	}
	//an edit and what follows it - the read again - with `busy` held across both
	async act( act:()=>Promise<unknown> ):Promise<void>{
		this.busy.set( true );
		this.error.set( undefined );
		try{
			await act();
		}
		catch( e ){
			this.error.set( errorMessage(e) );
		}
		finally{
			this.busy.set( false );
		}
	}
}