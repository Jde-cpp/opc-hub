import { Component, computed, inject, signal } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MAT_DIALOG_DATA, MatDialog, MatDialogActions, MatDialogContent, MatDialogRef, MatDialogTitle } from '@angular/material/dialog';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatIconModule } from '@angular/material/icon';
import { MatInputModule } from '@angular/material/input';
import { flowFaultText, FlowGraph, importFlow } from '../../../model/flow';
import { flowGraphFault } from '../../../model/flow-nodes';

export type FlowCreateDialogData = { import:boolean };
//the row to make, and for an import the graph its editor opens with
export type FlowCreated = { name:string; slug:string; description?:string; graph?:FlowGraph };

//a name as a slug:  lower case, what is no letter or digit a '-'
export const flowSlug = ( name:string ):string=>name.trim().toLowerCase().replace( /[^a-z0-9._]+/g, '-' ).replace( /^[-._]+|-+$/g, '' );
//the gateway's rule for a slug (FileSlugError):  it names the flow's trace directory
export const flowSlugError = ( slug:string ):string|undefined=>/^[A-Za-z0-9][A-Za-z0-9._-]*$/.test( slug ) ? undefined : "Letters, digits, '.', '_' and '-' only, starting with a letter or digit.";

//The Flows tab's Create and Import (#455):  a flow's name, its slug, which names its trace directory and never changes, and a
//description;  an import reads a flow's json first, an export of this editor's or a bare graph, and names the flow from it.  The
//graph is read, not deployed:  what the gateway would refuse of it is said here, and is the editor's to put right.
@Component({
	templateUrl: './flow-create-dialog.html',
	styles: `
		.fields{ display: grid; gap: .25rem; mat-form-field{ width: 100%; } }
		.file{ display: flex; align-items: center; gap: .75rem; margin-bottom: .75rem; input{ display: none; } }
		textarea{ font-family: 'Roboto Mono', monospace; font-size: 12px; }
		.read{ margin: 0 0 .75rem; color: var(--mat-sys-on-surface-variant); font: var(--mat-sys-body-medium); }
		.problem{ color: var(--mat-sys-error); }`,
	imports: [MatButtonModule, MatDialogActions, MatDialogContent, MatDialogTitle, MatFormFieldModule, MatIconModule, MatInputModule]
})
export class FlowCreateDialog{
	dialogRef = inject<MatDialogRef<FlowCreateDialog,FlowCreated|undefined>>( MatDialogRef );
	data = inject<FlowCreateDialogData>( MAT_DIALOG_DATA );
	name = signal( '' );
	slug = signal( '' );
	description = signal( '' );
	text = signal( '' );//an import's json
	#slugTyped = false;//the slug follows the name until it is typed
	//the import's json read:  its graph, or why it has none
	read = computed<{ graph?:FlowGraph; error?:string; name?:string; slug?:string; description?:string }>( ()=>{
		if( !this.text().trim() )
			return {};
		try{
			return importFlow( this.text() );
		}
		catch( e ){
			return { error: (<Error>e).message };
		}
	} );
	//what a deploy of the graph read would be refused by, which doesn't stop the import
	fault = computed<string|undefined>( ()=>{
		const graph = this.read().graph;
		const fault = graph ? flowGraphFault( graph ) : undefined;
		return fault ? flowFaultText( fault, graph ) : undefined;
	} );
	slugError = computed<string|undefined>( ()=>this.slug() ? flowSlugError( this.slug() ) : undefined );
	valid = computed<boolean>( ()=>!!this.name().trim() && !!this.slug() && !this.slugError() && (!this.data.import || !!this.read().graph) );

	setName( name:string ):void{
		this.name.set( name );
		if( !this.#slugTyped )
			this.slug.set( flowSlug( name ) );
	}
	setSlug( slug:string ):void{
		this.#slugTyped = true;
		this.slug.set( slug.trim() );
	}
	//the json given, typed or from a file:  it names the flow where the form hasn't yet
	setText( text:string ):void{
		this.text.set( text );
		const read = this.read();
		if( read.name && !this.name() )
			this.setName( read.name );
		if( read.slug && !this.#slugTyped && !flowSlugError( read.slug ) )
			this.slug.set( read.slug );
		if( read.description && !this.description() )
			this.description.set( read.description );
	}
	async onFile( input:HTMLInputElement ):Promise<void>{
		const file = input.files?.[0];
		if( file )
			this.setText( await file.text() );
		input.value = '';//the same file again is a change
	}
	apply():void{
		if( !this.valid() )
			return;
		const description = this.description().trim();
		this.dialogRef.close( { name: this.name().trim(), slug: this.slug(), ...( description ? {description} : {} ), ...( this.data.import ? {graph: this.read().graph} : {} ) } );
	}
	//the flow to make, or undefined for a cancel
	static open( dialog:MatDialog, data:FlowCreateDialogData ):Promise<FlowCreated|undefined>{
		const ref = dialog.open<FlowCreateDialog,FlowCreateDialogData,FlowCreated|undefined>( FlowCreateDialog, {data, width: '560px', maxWidth: '95vw', ariaModal: true} );
		return new Promise( resolve=>ref.afterClosed().subscribe( y=>resolve( y ) ) );
	}
}