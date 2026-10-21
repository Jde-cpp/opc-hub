import { Injectable, inject } from '@angular/core';
import {MatSnackBar} from '@angular/material/snack-bar';
import { Snackbar } from './snackbar';
import { errorMessage } from '../../utils/errors';

enum Type{
	Info = 'info-snackbar',
	Warning = 'warn-snackbar',
	Error = 'error-snackbar'
}
@Injectable({ providedIn: 'root' })
export class SnackbarService{
	#snackbar = inject( MatSnackBar );

	//DevTools anchors these console lines at this file, not at the caller.  To get the caller back, ignore-list it:  right-click the snackbar-service.ts frame in the console → "Add script to ignore list", or DevTools Settings → Ignore list → add a pattern.  Working around this is what the old `log:Log` parameter existed for.
	private static write( type:Type, message:string, detail?:unknown ):void{
		const args = detail===undefined || detail===message ? [message] : [message, detail];//log the throw itself, not its text:  DevTools renders an Error's stack expandable.
		switch( type ){
		case Type.Info:
			console.log( ...args );
			break;
		case Type.Warning:
			console.warn( ...args );
			break;
		case Type.Error:
			console.error( ...args );
			break;
		}
	}

	private show( userMessage:string, type:Type, detail?:unknown, duration?:number ){
		switch( type ){
		case Type.Info:
			duration = duration ?? 1000;
			break;
		case Type.Warning:
			duration = duration ?? 5000;
			break;
		case Type.Error:
			duration = duration ?? 10000;
			break;
		}
		SnackbarService.write( type, userMessage ?? "Unknown error", detail );
		this.#snackbar.openFromComponent( Snackbar, {panelClass: [type], data: {message: userMessage ?? "Unknown error", duration}} );
	}
	private showError( userMessage:string|undefined, detail:unknown ){
		this.show( userMessage ?? "Unknown error", Type.Error, detail );
	}

	assert( condition:boolean ):void{
		if( !condition ) {
			this.showError( "assert failed", "assert failed" );
			throw new Error( "assert failed" );
		}
	}
	//`info` is the caller's context ("Save failed.", "Could not load 'readers'").  It used to be dropped for every shape but
	//the last two, which meant the two that actually reach production - HttpErrorResponse and a {error:IError} rejection -
	//showed the bare server text with no clue which page or action produced it.  It prefixes the message now.
	private processEvent( e:unknown, type:Type=Type.Error, info?:string ):void{
		this.show( errorMessage(e, info), type, e );
	}

	exception( message:string, e:unknown ){
		this.processEvent( e, Type.Error, message );
	}

	error( message:string ){
		this.processEvent( message, Type.Error );
	}

	warn( message:unknown ){
		this.processEvent( message, Type.Warning );
	}

	info( message:unknown ):void{
		this.processEvent( message, Type.Info );
	}
}