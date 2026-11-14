import { Component, computed, input, output } from '@angular/core';
import { MatIconModule } from '@angular/material/icon';
import { MatTooltipModule } from '@angular/material/tooltip';
import { FFlowModule } from '@foblex/flow';
import { flowNodeType, flowOutputs } from '../../../../model/flow-nodes';
import { CanvasNode, inId, outId } from './flow-canvas-state';

//A node on the canvas, drawn by its type (#455):  its category's colour, its icon, its name or its type, an input port where
//the type takes one and an output port per output it has, a switch's a rule;  a warning where its props do not read, and an
//inject's button.  The canvas puts fNode on this host, and the ports here find it.  The library reads a node's text to name
//it, an icon's ligature included, so the host carries an aria-label.
@Component({
	selector: 'flow-node-view',
	template: `
		@if( type()?.input ){
			<div fConnector fConnectorType="target" [fConnectorId]="inPort()" [fConnectorMultiple]="true" fConnectorConnectableSide="left" class="port in"></div>
		}
		<mat-icon aria-hidden="true">{{ type()?.icon ?? 'help' }}</mat-icon>
		<span class="label">
			<span class="name">{{ node().name || node().type }}</span>
			@if( node().name ){ <span class="type">{{ node().type }}</span> }
		</span>
		@if( fault(); as f ){
			<mat-icon class="fault" [matTooltip]="f" aria-hidden="true">warning</mat-icon>
		}
		@if( node().type=='inject' ){
			<button type="button" class="inject" fDragBlocker [disabled]="!injectable()" (click)="inject.emit()" (keydown.enter)="$event.stopPropagation()" (keydown.space)="$event.stopPropagation()"
				[attr.aria-label]="'Inject ' + (node().name || 'a message')" [matTooltip]="injectable() ? 'Send its message' : 'Sends its message once the flow is deployed and enabled'"><mat-icon aria-hidden="true">play_arrow</mat-icon></button>
		}
		<div class="outs">
			@for( port of outPorts(); track port ){
				<div fConnector fConnectorType="source" [fConnectorId]="port" [fConnectorMultiple]="true" fConnectorConnectableSide="right" class="port out"></div>
			}
		</div>`,
	host: {
		'[class.source]': "type()?.category=='source'", '[class.sink]': "type()?.category=='sink'", '[class.unknown]': '!type()',
		'[class.faulty]': '!!fault()', '[class.refused]': 'refused()', '[class.tall]': 'outPorts().length>2',
		'[style.--flow-ports]': 'outPorts().length', '[attr.aria-label]': 'label()'
	},
	imports: [FFlowModule, MatIconModule, MatTooltipModule]
})
export class FlowNodeView{
	node = input.required<CanvasNode>();
	fault = input<string>();//what is wrong with its props, or its wires
	refused = input( false );//the node the gateway's refusal names
	injectable = input( false );
	inject = output<void>();
	type = computed( ()=>flowNodeType( this.node().type ) );
	inPort = computed( ()=>inId( this.node().id ) );
	outPorts = computed( ()=>Array.from( {length: flowOutputs( this.node() )}, ( _, port )=>outId( this.node().id, port ) ) );
	label = computed( ()=>{
		const node = this.node(), fault = this.fault();
		return `${node.name ? `${node.name}, ${node.type}` : node.type}${fault ? `.  ${fault}` : ''}`;
	} );
}