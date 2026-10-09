import { DatePipe } from '@angular/common';
import { Component, computed, inject, signal } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MAT_DIALOG_DATA, MatDialog, MatDialogActions, MatDialogContent, MatDialogRef, MatDialogTitle } from '@angular/material/dialog';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatIconModule } from '@angular/material/icon';
import { MatInputModule } from '@angular/material/input';
import { MatProgressBarModule } from '@angular/material/progress-bar';
import { MatSelectModule } from '@angular/material/select';
import { MatSlideToggleModule } from '@angular/material/slide-toggle';
import { errorText, ProtoUtils, Timestamp } from 'jde-framework';
import { editRefused, HistEditArgs, HistEditKind, HistEditResult, qlTime } from '../../../model/hist';
import { Variable } from '../../../model/node';
import { NodeId } from '../../../model/node-id';
import { OpcError } from '../../../model/opc-error';
import { ETypes, StatusCode } from '../../../model/types';
import { Value, valueString } from '../../../model/value';

//what a dialog opens on:  a row's node, time, value and status for a Replace, a time for a purge
export type HistEditPreset = { node?:Variable; time?:Date; end?:Date; value?:Value; status?:StatusCode };
export type HistEditDialogData = {
	kind:HistEditKind;
	nodes:Variable[];//the nodes this user may edit, the dialog's choice
	preset?:HistEditPreset;
	edit:( args:HistEditArgs )=>Promise<HistEditResult>;//sends it;  a throw is the gateway's refusal of the arguments
	names?:( codes:StatusCode[] )=>void;//asks for the names of the statuses the server answered, so a refusal reads by name
};

//A Date as <input type="datetime-local" step="0.001"> shows it, local time to the millisecond, and back.  Not toISOString,
//which is UTC and ends in Z, which the input refuses.
export function toLocalInput( d:Date ):string{
	const p = ( n:number, w=2 )=>String( n ).padStart( w, '0' );
	return `${p(d.getFullYear(), 4)}-${p(d.getMonth()+1)}-${p(d.getDate())}T${p(d.getHours())}:${p(d.getMinutes())}:${p(d.getSeconds())}.${p(d.getMilliseconds(), 3)}`;
}
export function fromLocalInput( s:string ):Date|undefined{
	if( !s )
		return undefined;
	const d = new Date( s );//no zone in the text, so local time, as the input means it
	return isNaN( d.getTime() ) ? undefined : d;
}
//a status as typed - hex with 0x, or decimal;  blank is Good.  null for text that is neither.
export function parseStatus( text:string ):StatusCode|undefined|null{
	const t = text.trim();
	if( !t )
		return undefined;
	const n = /^0x[0-9a-f]+$/i.test( t ) ? parseInt( t, 16 ) : /^\d+$/.test( t ) ? Number( t ) : NaN;
	return Number.isInteger( n ) && n>=0 && n<=0xFFFFFFFF ? n : null;
}

//One dialog for the five edits (spec *Edits*):  a node, the time, and for an UpdateData the value and its status;  a range
//or a list of times for a purge.  It sends the edit itself, so a refusal - a value the time already holds, a right the
//server withholds - shows beside the fields for another go, and only an edit the server took closes it.
@Component({
	templateUrl: './hist-edit-dialog.html',
	styleUrls: ['./hist-edit-dialog.scss'],
	imports: [DatePipe, MatButtonModule, MatDialogActions, MatDialogContent, MatDialogTitle, MatFormFieldModule, MatIconModule, MatInputModule, MatProgressBarModule, MatSelectModule, MatSlideToggleModule]
})
export class HistEditDialog{
	constructor(){
		const preset = this.data.preset ?? {};
		const now = new Date();
		this.nodes.set( preset.node ? [preset.node] : this.data.nodes.length==1 ? [this.data.nodes[0]] : [] );
		this.time.set( toLocalInput( preset.time ?? now ) );
		this.end.set( toLocalInput( preset.end ?? preset.time ?? now ) );
		this.times.set( [toLocalInput( preset.time ?? now )] );
		this.flag.set( preset.value===true );
		this.choice.set( typeof preset.value=="number" ? preset.value : undefined );
		const presetDate = preset.value!=null && typeof preset.value=="object" && "seconds" in preset.value ? ProtoUtils.toDate( <Timestamp>preset.value ) : null;
		this.text.set( presetDate ? toLocalInput( presetDate ) : preset.value!=undefined ? valueString( preset.value ) : "" );
		this.status.set( preset.status ? `0x${preset.status.toString( 16 ).toUpperCase().padStart( 8, '0' )}` : "" );
	}
	dialogRef = inject<MatDialogRef<HistEditDialog,HistEditResult|undefined>>( MatDialogRef );
	data = inject<HistEditDialogData>( MAT_DIALOG_DATA );
	get kind():HistEditKind{ return this.data.kind; }
	get purge():boolean{ return this.kind=='purgeRange' || this.kind=='purgeTimes'; }
	get title():string{ return HistEditDialog.titles[this.kind]; }
	get hint():string{ return HistEditDialog.hints[this.kind]; }
	get action():string{ return HistEditDialog.actions[this.kind]; }

	nodes = signal<Variable[]>( [] );//one for an UpdateData, any number for a purge
	node = computed<Variable|undefined>( ()=>this.nodes()[0] );
	time = signal( "" );//the UpdateData's source time, or the range's start, as the input holds it
	end = signal( "" );
	times = signal<string[]>( [] );
	text = signal( "" );//a number, string or DateTime value as typed
	flag = signal( false );//a Boolean's
	choice = signal<number|undefined>( undefined );//an enumeration's
	status = signal( "" );
	busy = signal( false );
	error = signal<string|undefined>( undefined );//the gateway's refusal of the call
	result = signal<HistEditResult|undefined>( undefined );//the server's refusal of a value or node, kept for another go

	onNodes( picked:Variable|Variable[] ){ this.nodes.set( Array.isArray( picked ) ? picked : [picked] ); }
	setTime( i:number, text:string ){ this.times.update( t=>t.map( (x, j)=>j==i ? text : x ) ); }
	addTime(){ this.times.update( t=>[...t, t[t.length-1] ?? toLocalInput( new Date() )] ); }
	removeTime( i:number ){ this.times.update( t=>t.length>1 ? t.filter( (_, j)=>j!=i ) : t ); }

	//the editor the value takes, by the node's DataType, as the Children tab's cells have it
	editor = computed<'boolean'|'enum'|'number'|'datetime'|'text'>( ()=>{
		const n = this.node();
		if( !n )
			return 'text';
		if( n.dataType==ETypes.Boolean )
			return 'boolean';
		if( n.customDataType && "enumValues" in n.customDataType )
			return 'enum';
		if( n.isInteger || n.isUnsigned || n.isFloating )
			return 'number';
		if( n.dataType==ETypes.DateTime || n.dataType==ETypes.UtcDateTime )
			return 'datetime';
		return 'text';
	} );
	enumValues = computed( ()=>{ const t = this.node()?.customDataType; return t && "enumValues" in t ? t.enumValues : []; } );
	value = computed<Value|undefined>( ()=>{
		switch( this.editor() ){
		case 'boolean': return this.flag();
		case 'enum': return this.choice();
		case 'number': return this.text().trim()=="" || isNaN( Number(this.text()) ) ? undefined : Number( this.text() );
		case 'datetime':{ const d = fromLocalInput( this.text() ); return d ? <Timestamp>qlTime( d ) : undefined; }
		default: return this.text();
		}
	} );
	//why the edit can't be sent yet, or nothing
	problem = computed<string|undefined>( ()=>{
		if( !this.nodes().length )
			return this.purge ? "Pick the nodes." : "Pick a node.";
		if( this.kind=='purgeTimes' )
			return this.times().every( fromLocalInput ) ? undefined : "Enter each time.";
		const start = fromLocalInput( this.time() );
		if( !start )
			return this.kind=='purgeRange' ? "Enter the start." : "Enter the source time.";
		if( this.kind=='purgeRange' ){
			const end = fromLocalInput( this.end() );
			return !end ? "Enter the end." : start>end ? "The start is after the end." : undefined;
		}
		if( this.value()===undefined )
			return "Enter a value.";
		return parseStatus( this.status() )===null ? "The status isn't a code - hex with 0x, or decimal." : undefined;
	} );
	args():HistEditArgs|undefined{
		if( this.problem() )
			return undefined;
		const nodes:NodeId[] = this.nodes().map( n=>n.nodeId );
		if( this.kind=='purgeRange' )
			return { kind: this.kind, nodes, start: fromLocalInput( this.time() )!, end: fromLocalInput( this.end() )! };
		if( this.kind=='purgeTimes' )
			return { kind: this.kind, nodes, times: this.times().map( t=>fromLocalInput( t )! ) };
		const status = parseStatus( this.status() );
		return { kind: this.kind, values: [{ node: nodes[0], source: fromLocalInput( this.time() )!, value: this.value()!, ...(status ? {status} : {}) }] };
	}
	async apply(){
		const args = this.args();
		if( !args || this.busy() )
			return;
		this.busy.set( true );
		this.error.set( undefined );
		this.result.set( undefined );
		try{
			const result = await this.data.edit( args );
			if( !editRefused( result ) )
				return this.dialogRef.close( result );
			this.result.set( result );
			this.data.names?.( [...result.values.map( v=>v.status ), ...result.nodes.map( n=>n.status )] );
		}
		catch( e ){
			this.error.set( errorText( e ) ?? "Unknown error" );
		}
		finally{
			this.busy.set( false );
		}
	}
	//the server's answer, a line per value or, for a range purge, per node
	rows = computed<{node:string; time:Date|null; status:StatusCode}[]>( ()=>{
		const r = this.result();
		if( !r )
			return [];
		const name = ( id:NodeId )=>[...this.data.nodes, ...this.nodes()].find( n=>n.key==id.key )?.name ?? id.toString();
		return r.values.length ? r.values.map( v=>({node: name( v.node ), time: v.source, status: v.status}) ) : r.nodes.map( n=>({node: name( n.node ), time: null, status: n.status}) );
	} );
	statusText( sc:StatusCode ):string{ return OpcError.text( sc ); }
	//Insert and Replace are Part 11's; Update is its insert-or-replace, which the spec names upsertHistory
	static readonly titles:Record<HistEditKind,string> = { insert: "Insert a value", replace: "Replace a value", update: "Update a value", purgeRange: "Delete a range", purgeTimes: "Delete at times" };
	static readonly actions:Record<HistEditKind,string> = { insert: "Insert", replace: "Replace", update: "Update", purgeRange: "Delete", purgeTimes: "Delete" };
	static readonly hints:Record<HistEditKind,string> = {
		insert: "Adds a value at a time that holds none.",
		replace: "Replaces the value at a time that holds one.",
		update: "Replaces the value at the time, or inserts it when the time holds none.",
		purgeRange: "Deletes each node's values from the start to the end.  Whether the end is included is the server's rule - the bundled OPC server leaves it out - and a start equal to the end deletes the one value there.",
		purgeTimes: "Deletes each node's value at each time.  Not every server supports this:  the bundled OPC server refuses it, and a range from a time to itself deletes the one value instead."
	};
	static open( dialog:MatDialog, data:HistEditDialogData ):Promise<HistEditResult|undefined>{
		const ref = dialog.open<HistEditDialog,HistEditDialogData,HistEditResult|undefined>( HistEditDialog, {data, width: '480px', ariaModal: true} );
		return new Promise( resolve=>ref.afterClosed().subscribe( r=>resolve( r ?? undefined ) ) );
	}
}