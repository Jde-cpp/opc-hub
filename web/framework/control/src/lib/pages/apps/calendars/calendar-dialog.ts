import { Directive, inject, signal } from '@angular/core';
import { MatDialogRef } from '@angular/material/dialog';
import { errorMessage } from '../../../utils/errors';

//What the calendar's form dialogs share:  a send that holds the dialog open until the server answers.
@Directive()
export abstract class CalendarDialog<R>{
	#ref = inject<MatDialogRef<unknown,R>>( MatDialogRef );
	busy = signal( false );
	error = signal<string|undefined>( undefined );

	//`work`'s answer closes the dialog with it, and none leaves it open.  Its throw is the server's refusal, said on the form.
	protected async send( work:()=>Promise<R|undefined> ):Promise<void>{
		if( this.busy() )
			return;
		this.busy.set( true );
		this.#ref.disableClose = true;//Escape or the backdrop would close it on a save the server may still take, and the page never read again
		this.error.set( undefined );
		try{
			const answer = await work();
			if( answer!==undefined )
				this.#ref.close( answer );
		}
		catch( e ){
			this.error.set( errorMessage(e) );
		}
		finally{
			this.#ref.disableClose = false;
			this.busy.set( false );
		}
	}
}