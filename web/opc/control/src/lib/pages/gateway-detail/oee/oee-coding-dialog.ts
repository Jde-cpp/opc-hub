import { DatePipe } from '@angular/common';
import { Component, computed, inject, signal } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MatCheckboxModule } from '@angular/material/checkbox';
import { MAT_DIALOG_DATA, MatDialog, MatDialogActions, MatDialogContent, MatDialogRef, MatDialogTitle } from '@angular/material/dialog';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatIconModule } from '@angular/material/icon';
import { MatInputModule } from '@angular/material/input';
import { MatProgressBarModule } from '@angular/material/progress-bar';
import { MatSelectModule } from '@angular/material/select';
import { DateUtils, errorMessage } from 'jde-framework';
import { OeeCoding, OeeCodingCall, oeeCodingCalls, OeeCodingTarget, oeeDuration, OeeReasonRef, oeeSplitProblem } from '../../../model/oee';

//the stop a dialog opens on, with the codings over it
export type OeeCodingStop = OeeCodingTarget & { codings:OeeCoding[] };
export type OeeCodingDialogData = {
	stop:OeeCodingStop;
	reasons:OeeReasonRef[];//the live ones, the dialog's choice
	code:( call:OeeCodingCall )=>Promise<void>;//sends one;  a throw is the gateway's refusal
	uncode:( id:number )=>Promise<void>;
};

//The coding dialog (#405, docs/oee/spec.md *Reasons and codings*, *Web*), of the history edit dialog's shape:  a reason and a
//comment over the stop, and for a stop that had two causes a split time, with a reason and a comment for the part after it.  It
//sends codeOee itself, once or twice, so a refusal - a coding already over part of the stop, a right withheld - shows beside the
//fields for another go;  a second call refused leaves the first part coded, and another go sends both again, the first
//replacing itself.  The codings already over the stop are listed, each with its removal, uncodeOee.  `open` answers whether
//anything landed, for the page to read its chart and its table again.
@Component({
	templateUrl: './oee-coding-dialog.html',
	styleUrls: ['./oee-coding-dialog.scss'],
	imports: [DatePipe, MatButtonModule, MatCheckboxModule, MatDialogActions, MatDialogContent, MatDialogTitle, MatFormFieldModule, MatIconModule, MatInputModule, MatProgressBarModule, MatSelectModule]
})
export class OeeCodingDialog{
	constructor(){
		const {stop} = this.data, only = stop.codings.length==1 ? stop.codings[0] : undefined;
		this.codings.set( stop.codings );
		//one coding over it is what the dialog edits;  a deleted reason has left the choice
		this.reason.set( this.data.reasons.find( r=>r.id==only?.reason?.id )?.id );
		this.comment.set( only?.comment ?? '' );
		this.splitAt.set( DateUtils.toLocalInput( new Date( ( stop.start.getTime()+( stop.end ?? new Date() ).getTime() )/2 ) ) );
	}
	dialogRef = inject<MatDialogRef<OeeCodingDialog,boolean>>( MatDialogRef );
	data = inject<OeeCodingDialogData>( MAT_DIALOG_DATA );
	get stop():OeeCodingStop{ return this.data.stop; }
	length = oeeDuration( ( this.data.stop.end ?? new Date() ).getTime()-this.data.stop.start.getTime() );

	reason = signal<number|undefined>( undefined );
	comment = signal( '' );
	split = signal( false );
	splitAt = signal( '' );//as the input holds it
	reason2 = signal<number|undefined>( undefined );
	comment2 = signal( '' );
	codings = signal<OeeCoding[]>( [] );//those over the stop, less the ones removed here
	busy = signal( false );
	error = signal<string|undefined>( undefined );

	//why it can't be sent yet, or nothing
	problem = computed<string|undefined>( ()=>{
		if( this.reason()==undefined )
			return 'Pick a reason.';
		if( !this.split() )
			return undefined;
		return oeeSplitProblem( this.stop, DateUtils.fromLocalInput( this.splitAt() ) ) ?? ( this.reason2()==undefined ? 'Pick the reason after the split.' : undefined );
	} );
	calls():OeeCodingCall[]|undefined{
		if( this.problem() )
			return undefined;
		const first = { reason: this.reason()!, comment: this.comment() };
		return oeeCodingCalls( this.stop, first, this.split() ? {at: DateUtils.fromLocalInput( this.splitAt() )!, second: {reason: this.reason2()!, comment: this.comment2()}} : undefined );
	}
	async apply():Promise<void>{
		const calls = this.calls();
		if( !calls || this.busy() )
			return;
		await this.#send( async ()=>{
			for( const [i, call] of calls.entries() ){
				try{
					await this.data.code( call );
				}
				catch( e ){
					throw new Error( i ? `The part before the split is coded;  the part after it was refused:  ${errorMessage(e)}` : errorMessage(e) );
				}
			}
			this.dialogRef.close( true );
		} );
	}
	//a coding over the stop taken away:  the way back from a split, since a coding over the whole stop is refused while another covers part of it
	async remove( c:OeeCoding ):Promise<void>{
		if( this.busy() )
			return;
		await this.#send( async ()=>{
			await this.data.uncode( c.id );
			this.codings.update( list=>list.filter( x=>x.id!=c.id ) );
		} );
	}
	async #send( act:()=>Promise<void> ):Promise<void>{
		this.busy.set( true );
		this.dialogRef.disableClose = true;//Escape or the backdrop would close it on a coding the gateway may still take, with no refresh
		this.error.set( undefined );
		try{
			await act();
		}
		catch( e ){
			this.error.set( errorMessage(e) );
		}
		finally{
			this.busy.set( false );
			this.dialogRef.disableClose = false;
		}
	}
	//True where a coding or a removal landed, however it closed:  Escape after a removal, or after the first of two codings, too.
	static open( dialog:MatDialog, data:OeeCodingDialogData ):Promise<boolean>{
		let changed = false;
		const landed = <A extends unknown[]>( send:( ...args:A )=>Promise<void> )=>async ( ...args:A )=>{ await send( ...args ); changed = true; };
		const ref = dialog.open<OeeCodingDialog,OeeCodingDialogData,boolean>( OeeCodingDialog, {data: {...data, code: landed( data.code ), uncode: landed( data.uncode )}, width: '480px', ariaModal: true} );
		return new Promise( resolve=>ref.afterClosed().subscribe( ()=>resolve( changed ) ) );
	}
}