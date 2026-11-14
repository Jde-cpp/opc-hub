import { ComponentRef, ViewContainerRef } from '@angular/core';
import type { FlowCanvas } from './canvas/flow-canvas';

//The canvas, read-only, for a view of a flow's activity or history (#456):  imported once the tab is shown, as the editor
//imports it, so the library sits in neither main nor the page's chunk.
export async function loadFlowView( slot:ViewContainerRef ):Promise<ComponentRef<FlowCanvas>>{
	const { FlowCanvas } = await import( './canvas/flow-canvas' );
	const canvas = slot.createComponent( FlowCanvas );
	canvas.setInput( 'readonly', true );
	return canvas;
}