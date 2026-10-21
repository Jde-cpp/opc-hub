import { signal } from '@angular/core';
import { errorMessage, SnackbarService } from 'jde-framework';

//A detail page's Members tab, as a template's and a group's have it (#337):  the rows, a read or an edit in flight, and the gateway's
//refusal of the read.  Each page gives the calls and the words;  its columns stay its own.
export class HistMembers<T>{
	constructor( private snackbar:SnackbarService ){}
	rows = signal<T[]>( [] );
	loading = signal( false );
	busy = signal( false );
	error = signal<string|undefined>( undefined );

	//`read` answers what the tab shows, and `land` takes it only while `current()` holds:  the router reuses the page for another
	//record meanwhile, maybe another gateway's with the same id.
	async read<R>( read:()=>Promise<R>, land:( answer:R )=>void, current:()=>boolean ):Promise<void>{
		this.loading.set( true );
		this.error.set( undefined );
		try{
			const answer = await read();
			if( current() )
				land( answer );
		}
		catch( e ){
			this.error.set( errorMessage(e) );
		}
		finally{
			this.loading.set( false );
		}
	}
	//a mutation and what follows it - the read again - with `busy` held across both;  a refusal is a toast
	async mutate( act:()=>Promise<unknown>, refused:string ):Promise<void>{
		this.busy.set( true );
		try{
			await act();
		}
		catch( e ){
			this.snackbar.exception( refused, e );
		}
		finally{
			this.busy.set( false );
		}
	}
	//The picks added one at a time, each its own mutation:  one refused - the same path or node again, a right withheld - doesn't
	//stop the rest.  Then the count added, or the first few refusals together, each by its pick's label.
	async add<P>( picks:P[], noun:string, label:( pick:P )=>string, add:( pick:P )=>Promise<unknown> ):Promise<void>{
		this.busy.set( true );
		const refusals:string[] = [];
		try{
			for( const p of picks ){
				try{
					await add( p );
				}
				catch( e ){
					refusals.push( `${label( p )}:  ${errorMessage(e)}` );
				}
			}
		}
		finally{
			this.busy.set( false );
		}
		if( refusals.length )
			this.snackbar.error( `${picks.length-refusals.length} of ${picks.length} added.  ${refusals.slice( 0, histRefusalsShown ).join( '  ' )}` );
		else
			this.snackbar.info( `${picks.length} ${picks.length==1 ? noun : `${noun}s`} added.` );
	}
}
export const histRefusalsShown = 3;