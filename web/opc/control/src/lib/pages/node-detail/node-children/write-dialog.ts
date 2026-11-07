import { Component, computed, inject, signal } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MAT_DIALOG_DATA, MatDialog, MatDialogActions, MatDialogContent, MatDialogRef, MatDialogTitle } from '@angular/material/dialog';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatIconModule } from '@angular/material/icon';
import { MatInputModule } from '@angular/material/input';
import { MatProgressBarModule } from '@angular/material/progress-bar';
import { errorMessage } from 'jde-framework';
import { Enum } from '../../../model/enum';
import { Variable } from '../../../model/node';
import { NodeView } from '../../../model/node-view';
import { ETypes } from '../../../model/types';
import { Reading, Value } from '../../../model/value';
import { ValueEditor } from '../value-editor/value-editor';

//what the dialog opens on:  the row, and how to write it, so a spec stands in for the gateway with one function
export type WriteDialogData = {
	node:Variable;
	write:( value:Value, indexRange?:string )=>Promise<Reading>;//updateVariable;  a throw is the gateway's or the server's refusal
};

//Part 4 §7.27's NumericRange, one dimension:  an index, or a range `low:high`, low less than high.  The elements it names, from the
//array's length;  none for text that is no range, and an empty list for one past the array.
export function indexRangeOf( text:string, length:number ):{start:number; count:number}|undefined{
	const m = /^(\d+)(?::(\d+))?$/.exec( text.trim() );
	if( !m )
		return undefined;
	const start = +m[1], end = m[2]===undefined ? start : +m[2];
	if( end<start || (m[2]!==undefined && end==start) )
		return undefined;
	return { start, count: Math.max( Math.min( end, length-1 )-start+1, 0 ) };
}

//A Variable row's Write (clients spec *Values and writes*, #423):  the value as its type is typed (ValueEditor), opened on the row's
//reading;  an array whole, or the slice an index range names, Part 4's "2:4", prefilled from the elements it has.  It sends the write
//itself, so a refusal - the server's, or the gateway's of the value - shows beside the fields for another go;  a write the server took
//closes it with the value read back, which the row then shows.
@Component({
	templateUrl: './write-dialog.html',
	styleUrls: ['./write-dialog.scss'],
	imports: [MatButtonModule, MatDialogActions, MatDialogContent, MatDialogTitle, MatFormFieldModule, MatIconModule, MatInputModule, MatProgressBarModule, ValueEditor]
})
export class WriteDialog{
	dialogRef = inject<MatDialogRef<WriteDialog,Reading|undefined>>( MatDialogRef );
	data = inject<WriteDialogData>( MAT_DIALOG_DATA );
	get node():Variable{ return this.data.node; }
	get title():string{ return `Write ${this.node.name}`; }
	//the type as the Data Type column spells it, and `[]` for an array
	get typeName():string{ return `${NodeView.dataType( this.node ) ?? ""}${this.isArray ? "[]" : ""}`; }
	get isArray():boolean{ return this.node.isArray || (this.node.valueRank!=undefined && this.node.valueRank>=0); }
	//the built-in type the editor edits as:  none for a type of the server's own, whose value's shape decides
	get type():ETypes|undefined{ return this.node.customDataType ? undefined : this.node.dataType; }
	get enumeration():Enum|undefined{ const t = this.node.customDataType; return t && "enumValues" in t ? t : undefined; }

	indexRange = signal( "" );
	value = signal<Value|null|undefined>( undefined );
	busy = signal( false );
	error = signal<string|undefined>( undefined );
	constructor(){
		this.value.set( this.node.value );
	}
	//the elements the index range names, as the array holds them - the editor's value while a range is typed;  the slice is edited at
	//its own length
	range = computed( ()=>this.isArray && this.indexRange().trim() ? indexRangeOf( this.indexRange(), Array.isArray( this.node.value ) ? this.node.value.length : 0 ) : undefined );
	setIndexRange( text:string ){
		this.indexRange.set( text );
		const r = this.isArray && text.trim() ? indexRangeOf( text, Array.isArray( this.node.value ) ? this.node.value.length : 0 ) : undefined;
		const whole = Array.isArray( this.node.value ) ? this.node.value : [];
		this.value.set( r ? whole.slice( r.start, r.start+r.count ) : text.trim() ? undefined : this.node.value );
	}
	//why the write can't be sent yet, or nothing
	problem = computed<string|undefined>( ()=>{
		if( this.isArray && this.indexRange().trim() && !this.range() )
			return "The index range is an index, or low:high.";
		if( this.range() && !this.range()!.count )
			return "The index range is past the array.";
		return this.value()===undefined ? "Enter a value." : undefined;
	} );
	async apply(){
		if( this.problem() || this.busy() )
			return;
		this.busy.set( true );
		this.dialogRef.disableClose = true;//Escape or the backdrop would close it on a write the server may still take, and the row never hear of
		this.error.set( undefined );
		try{
			const reading = await this.data.write( <Value>this.value(), this.range() ? this.indexRange().trim() : undefined );
			this.dialogRef.close( reading );
		}
		catch( e ){
			this.error.set( errorMessage( e ) );
		}
		finally{
			this.busy.set( false );
			this.dialogRef.disableClose = false;
		}
	}
	static open( dialog:MatDialog, data:WriteDialogData ):Promise<Reading|undefined>{
		const ref = dialog.open<WriteDialog,WriteDialogData,Reading|undefined>( WriteDialog, {data, width: '480px', ariaModal: true} );
		return new Promise( resolve=>ref.afterClosed().subscribe( r=>resolve( r ?? undefined ) ) );
	}
}