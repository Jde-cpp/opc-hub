import { DatePipe } from '@angular/common';
import { Component, computed, inject, linkedSignal, Signal, signal } from '@angular/core';
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
//the live reasons, the dialog's choice, as the page's read of them stands:  still reading, refused, or read
export type OeeReasonsRead = { list:OeeReasonRef[]; reading?:boolean; error?:string };
export type OeeCodingDialogData = {
	stop:OeeCodingStop;
	reasons:Signal<OeeReasonsRead>;
	code:( call:OeeCodingCall )=>Promise<OeeCoding|undefined>;//sends one, answering the coding written;  a throw is the gateway's refusal
	uncode:( id:number )=>Promise<void>;
	reread:()=>Promise<OeeCodingStop|undefined>;//the stop as the gateway has it now, none where it is gone
};

//The coding dialog (#405, docs/oee/spec.md *Reasons and codings*, *Web*), of the history edit dialog's shape:  a reason and a
//comment over the stop, and for a stop that had two causes a split time, with a reason and a comment for the part after it.  It
//sends codeOee itself, once or twice, so a refusal - a coding already over part of the stop, a right withheld - shows beside the
//fields for another go;  a second call refused leaves the first part coded, and another go sends both again, the first
//replacing itself.  The codings already over the stop are listed, each with its removal, uncodeOee, and each that lands takes
//its place in the list.  A stop open when the page read it is read again before it is coded, so the coding ends where the stop
//did, not at a now past it.  `open` answers whether anything landed, for the page to read its chart and its table again.
@Component({
	templateUrl: './oee-coding-dialog.html',
	styleUrls: ['./oee-coding-dialog.scss'],
	imports: [DatePipe, MatButtonModule, MatCheckboxModule, MatDialogActions, MatDialogContent, MatDialogTitle, MatFormFieldModule, MatIconModule, MatInputModule, MatProgressBarModule, MatSelectModule]
})
export class OeeCodingDialog{
	constructor(){
		const {stop} = this.data;
		this.codings.set( stop.codings );
		this.comment.set( this.#only?.comment ?? '' );
		this.#splitFrom = new Date( ( stop.start.getTime()+( stop.end ?? new Date() ).getTime() )/2 );
		this.splitAt.set( DateUtils.toLocalInput( this.#splitFrom ) );
	}
	dialogRef = inject<MatDialogRef<OeeCodingDialog,boolean>>( MatDialogRef );
	data = inject<OeeCodingDialogData>( MAT_DIALOG_DATA );
	stop = signal<OeeCodingStop>( this.data.stop );//as last read
	readonly length = computed( ()=>{ const s = this.stop(); return oeeDuration( ( s.end ?? new Date() ).getTime()-s.start.getTime() ); } );
	readonly reasons = computed( ()=>this.data.reasons().list );
	//why the choice is empty:  the read under way, refused, or no reasons on the gateway
	readonly reasonsHint = computed( ()=>{
		const r = this.data.reasons();
		return r.error ? `Could not list the reasons:  ${r.error}` : r.reading ? "Reading the gateway's reasons." : r.list.length ? undefined : 'The gateway has no reasons:  add them on its Reasons tab.';
	} );
	//one coding over it is what the dialog edits;  a deleted reason has left the choice.  Set again once the reasons are read.
	#only = this.data.stop.codings.length==1 ? this.data.stop.codings[0] : undefined;
	reason = linkedSignal<number|undefined>( ()=>this.reasons().find( r=>r.id==this.#only?.reason?.id )?.id );
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
		return oeeSplitProblem( this.stop(), this.#splitTime() ) ?? ( this.reason2()==undefined ? 'Pick the reason after the split.' : undefined );
	} );
	//the split time typed:  `from`, the time the input opened on, keeps a time in the hour DST repeats in the pass it was in
	#splitTime():Date|undefined{ return DateUtils.fromLocalInput( this.splitAt(), this.#splitFrom ); }
	#splitFrom:Date;
	calls():OeeCodingCall[]|undefined{
		if( this.problem() )
			return undefined;
		const first = { reason: this.reason()!, comment: this.comment() };
		return oeeCodingCalls( this.stop(), first, this.split() ? {at: this.#splitTime()!, second: {reason: this.reason2()!, comment: this.comment2()}} : undefined );
	}
	async apply():Promise<void>{
		if( this.problem() || this.busy() )
			return;
		await this.#send( async ()=>{
			if( !this.stop().endText ){//open as the page read it:  where it has since ended is where the coding ends
				const now = await this.data.reread();
				if( !now )
					throw new Error( 'The stop is gone:  the engine has made its stops again.  Close, and code it from the page read again.' );
				this.stop.set( now );
				this.codings.set( now.codings );
			}
			const calls = this.calls();
			if( !calls )
				throw new Error( `The stop has ended:  ${this.problem()}` );
			for( const [i, call] of calls.entries() ){
				try{
					const written = await this.data.code( call );
					if( written )//in place of the one it replaced, which keeps its id
						this.codings.update( list=>[...list.filter( c=>c.id!=written.id ), written].sort( ( a, b )=>a.start.getTime()-b.start.getTime() ) );
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
		const landed = <A extends unknown[], R>( send:( ...args:A )=>Promise<R> )=>async ( ...args:A )=>{ const y = await send( ...args ); changed = true; return y; };
		const ref = dialog.open<OeeCodingDialog,OeeCodingDialogData,boolean>( OeeCodingDialog, {data: {...data, code: landed( data.code ), uncode: landed( data.uncode )}, width: '480px', ariaModal: true} );
		return new Promise( resolve=>ref.afterClosed().subscribe( ()=>resolve( changed ) ) );
	}
}