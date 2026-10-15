import { WritableSignal } from '@angular/core';
import { errorText, SnackbarService } from 'jde-framework';

export type HistAddEachArgs<T> = { noun:string; label:( pick:T )=>string; add:( pick:T )=>Promise<unknown>; busy:WritableSignal<boolean>; snackbar:SnackbarService };
//The picks added one at a time, each its own mutation (#337):  one refused - the same path or node again, a right withheld - doesn't
//stop the rest.  Then the count added, or the first few refusals together, each by its pick's label.
export async function histAddEach<T>( picks:T[], args:HistAddEachArgs<T> ):Promise<void>{
	args.busy.set( true );
	const refusals:string[] = [];
	try{
		for( const p of picks ){
			try{
				await args.add( p );
			}
			catch( e ){
				refusals.push( `${args.label( p )}:  ${errorText( e ) ?? 'Unknown error'}` );
			}
		}
	}
	finally{
		args.busy.set( false );
	}
	if( refusals.length )
		args.snackbar.error( `${picks.length-refusals.length} of ${picks.length} added.  ${refusals.slice( 0, histRefusalsShown ).join( '  ' )}` );
	else
		args.snackbar.info( `${picks.length} ${picks.length==1 ? args.noun : `${args.noun}s`} added.` );
}
export const histRefusalsShown = 3;