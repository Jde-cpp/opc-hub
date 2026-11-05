import { Component, computed, inject, OnInit, signal } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MAT_DIALOG_DATA, MatDialog, MatDialogActions, MatDialogContent, MatDialogRef, MatDialogTitle } from '@angular/material/dialog';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatIconModule } from '@angular/material/icon';
import { MatInputModule } from '@angular/material/input';
import { MatProgressBarModule } from '@angular/material/progress-bar';
import { MatSelectModule } from '@angular/material/select';
import { MatSlideToggleModule } from '@angular/material/slide-toggle';
import { errorMessage } from 'jde-framework';
import { Argument, argumentValue, CallResult, MethodArguments } from '../../../model/method';
import { Method, UaNode } from '../../../model/node';
import { OpcError } from '../../../model/opc-error';
import { isBad, statusIcon } from '../../../model/status-code';
import { StatusCode } from '../../../model/types';
import { Value } from '../../../model/value';

//what the dialog opens on:  the object and its method, how to read the method's arguments and how to call it, so a spec stands
//in for the gateway with two functions
export type MethodCallDialogData = {
	object:UaNode;
	method:Method;
	arguments:()=>Promise<MethodArguments>;
	call:( args:Value[] )=>Promise<CallResult>;//sends it;  a throw is the gateway's refusal before any call
	names?:( codes:StatusCode[] )=>void;//asks for the names of the statuses the server answered, so a refusal reads by name
};

//A Method row's Call (clients spec *Methods*, #318):  one editor per input argument, named and described as the server
//declares them, the call's status, each argument's where the server refused one, and the outputs by name.  It stays open on
//an answer, good or refused, for another go with the arguments as they were.
@Component({
	templateUrl: './method-call-dialog.html',
	styleUrls: ['./method-call-dialog.scss'],
	imports: [MatButtonModule, MatDialogActions, MatDialogContent, MatDialogTitle, MatFormFieldModule, MatIconModule, MatInputModule, MatProgressBarModule, MatSelectModule, MatSlideToggleModule]
})
export class MethodCallDialog implements OnInit{
	dialogRef = inject<MatDialogRef<MethodCallDialog,CallResult|undefined>>( MatDialogRef );
	data = inject<MethodCallDialogData>( MAT_DIALOG_DATA );
	ngOnInit(){ this.load(); }
	get title():string{ return `${this.data.object.name}.${this.data.method.name}`; }

	arguments = signal<MethodArguments|undefined>( undefined );
	loadError = signal<string|undefined>( undefined );//the arguments could not be read
	texts = signal<string[]>( [] );//each input as typed - a number, string, date or json - or an enumeration's chosen id
	flags = signal<boolean[]>( [] );//a Boolean input's
	busy = signal( false );
	error = signal<string|undefined>( undefined );//the gateway's refusal of the call
	result = signal<CallResult|undefined>( undefined );//the server's answer, kept for another go

	async load(){
		try{
			const a = await this.data.arguments();
			this.arguments.set( a );
			this.texts.set( a.inputs.map( ()=>"" ) );
			this.flags.set( a.inputs.map( ()=>false ) );
		}
		catch( e ){
			this.loadError.set( errorMessage( e ) );
		}
	}
	get inputs():Argument[]{ return this.arguments()?.inputs ?? []; }
	setText( i:number, text:string ){ this.texts.update( t=>t.map( (x, j)=>j==i ? text : x ) ); }
	setFlag( i:number, flag:boolean ){ this.flags.update( f=>f.map( (x, j)=>j==i ? flag : x ) ); }
	//each input's value as typed, undefined where the text is no value of its type
	values = computed<( Value|undefined )[]>( ()=>this.inputs.map( (a, i)=>argumentValue( a, this.texts()[i] ?? "", this.flags()[i] ?? false ) ) );
	//why the call can't be sent yet, or nothing
	problem = computed<string|undefined>( ()=>{
		if( this.loadError() )
			return "The arguments could not be read.";
		if( !this.arguments() )
			return "Reading the arguments.";
		const missing = this.inputs.filter( (_, i)=>this.values()[i]===undefined );
		return missing.length ? `Enter ${missing.map( a=>a.name ).join( ", " )}.` : undefined;
	} );
	label( a:Argument ):string{ return `${a.name} (${a.typeName}${a.isArray ? "[]" : ""})`; }
	hint( a:Argument ):string{ return a.editor=='json' ? `${a.description} As json, [1, 2] say.`.trim() : a.description; }
	async apply(){
		if( this.problem() || this.busy() )
			return;
		this.busy.set( true );
		this.dialogRef.disableClose = true;//Escape or the backdrop would close it on a call the server may still make
		this.error.set( undefined );
		this.result.set( undefined );
		try{
			const result = await this.data.call( <Value[]>this.values() );
			this.result.set( result );
			const codes = [result.statusCode, ...result.inputArgumentResults].filter( sc=>sc );//Good needs no name
			if( codes.length )
				this.data.names?.( codes );
		}
		catch( e ){
			this.error.set( errorMessage( e ) );
		}
		finally{
			this.busy.set( false );
			this.dialogRef.disableClose = false;
		}
	}
	//the server's answer:  each argument's status where it refused one, and the outputs by name
	argumentResults = computed<{name:string; status:StatusCode}[]>( ()=>( this.result()?.inputArgumentResults ?? [] ).map( (status, i)=>({name: this.inputs[i]?.name ?? `argument ${i+1}`, status}) ) );
	//none for a refused call:  open62541 answers one with an empty value per declared output, and Part 4 §5.11.2 has the outputs
	//only for a call that succeeded (reviews-issue/318-opc-calls.md W1)
	outputs = computed<{name:string; text:string}[]>( ()=>{
		const r = this.result();
		if( !r || isBad( r.statusCode ) )
			return [];
		const declared = this.arguments()?.outputs ?? [];
		return r.outputArguments.map( (v, i)=>({name: declared[i]?.name ?? `output ${i+1}`, text: ( declared[i] ?? new Argument( {} ) ).valueText( v )}) );
	} );
	statusText( sc:StatusCode ):string{ return OpcError.text( sc ); }
	statusIcon( sc:StatusCode ){ return statusIcon( sc ); }
	static open( dialog:MatDialog, data:MethodCallDialogData ):Promise<CallResult|undefined>{
		const ref = dialog.open<MethodCallDialog,MethodCallDialogData,CallResult|undefined>( MethodCallDialog, {data, width: '480px', ariaModal: true} );
		return new Promise( resolve=>ref.afterClosed().subscribe( r=>resolve( r ?? undefined ) ) );
	}
}