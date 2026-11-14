import { afterNextRender, Component, computed, ElementRef, input, OnDestroy, output, signal, viewChild, ViewEncapsulation } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MatIconModule } from '@angular/material/icon';
import { errorMessage } from 'jde-framework';
import { FlowBlocksView, newFlowBlocks } from './flow-blocks';
import type { FlowBlocksResult } from './flow-blocks-loader';

//A Function node's body as blocks (#458, docs/flows/spec.md *The Function node*, *Web*):  the workspace with its toolbox and,
//beside it, the code the blocks write, read-only, which Done hands back with the workspace;  or, for the history, the
//workspace as it was deployed, to be read alone, with the blocks a message passed lit and the rest dimmed.  It takes the
//room its tab's canvas has, over it, and is made by loadFlowBlocks alone.  Its sheet is not encapsulated, since the blocks
//are Blockly's own elements;  every rule is under the component's name.
@Component({
	selector: 'flow-block-editor',
	templateUrl: './flow-block-editor.html',
	styleUrl: './flow-block-editor.scss',
	encapsulation: ViewEncapsulation.None,
	imports: [MatButtonModule, MatIconModule]
})
export class FlowBlockEditor implements OnDestroy{
	title = input.required<string>();
	blocks = input<unknown>( undefined );//the workspace saved:  none is a new body's
	code = input( '' );//the node's, which a read-only view shows
	readonly = input( false );
	lit = input<string[]>( [] );//the ids of the blocks a message passed
	done = output<FlowBlocksResult>();
	closed = output<void>();
	host = viewChild.required<ElementRef<HTMLElement>>( 'host' );
	first = viewChild.required<string,ElementRef<HTMLElement>>( 'first', {read: ElementRef} );

	generated = signal( '' );
	fault = signal<string|undefined>( undefined );//the workspace did not read
	passed = signal( 0 );//the lit ids that are blocks of the workspace
	shown = computed( ()=>this.readonly() ? this.code() : this.generated() );
	hint = computed( ()=>{
		if( !this.readonly() )
			return 'Done writes the code beside the blocks;  the node keeps what it has until then.';
		const lit = this.lit().length, passed = this.passed();
		return `The message passed ${lit} ${lit==1 ? 'block' : 'blocks'}${passed<lit ? `, ${lit-passed} of them not in this workspace` : ''}.`;
	} );
	#view?:FlowBlocksView;
	#resized?:ResizeObserver;

	constructor(){ afterNextRender( ()=>this.#open() ); }
	ngOnDestroy(){
		this.#resized?.disconnect();
		this.#view?.dispose();
	}
	#open():void{
		const host = this.host().nativeElement;
		try{
			const view = this.#view = new FlowBlocksView( host, this.blocks() ?? newFlowBlocks(), this.readonly() );
			if( typeof ResizeObserver!='undefined' ){
				this.#resized = new ResizeObserver( ()=>view.resize() );
				this.#resized.observe( host );
			}
			if( this.readonly() ){
				this.passed.set( view.light( this.lit() ) );
				this.first().nativeElement.focus();
			}
			else{
				this.generated.set( view.code() );
				view.onChange( code=>this.generated.set( code ) );
				view.focus();
			}
		}
		catch( e ){
			this.fault.set( errorMessage( e, 'The blocks do not read.' ) );
			this.first().nativeElement.focus();
		}
	}
	finish():void{
		if( this.#view )
			this.done.emit( {blocks: this.#view.save(), code: this.#view.code()} );
	}
}