import { ComponentRef, Service, ViewContainerRef } from '@angular/core';
import type { FlowBlockEditor } from './flow-block-editor';

//what the block editor answers at Done:  the workspace, and the code the generator wrote of it
export type FlowBlocksResult = { blocks:object; code:string };

//The block editor of a Function node's body (#458), imported once it is asked for, by the editor's form or the history's
//lineage, as the canvas is by its tabs (flow-view.ts):  Blockly is a megabyte with its blocks and generator, and sits in
//neither main nor the page's chunk, which every tab preloads.  A service, so a page's spec stands a stub in its place and
//leaves Blockly to the editor's own.
@Service()
export class FlowBlocksLoader{
	async load( slot:ViewContainerRef ):Promise<ComponentRef<FlowBlockEditor>>{
		const { FlowBlockEditor } = await import( './flow-block-editor' );
		return slot.createComponent( FlowBlockEditor );
	}
}