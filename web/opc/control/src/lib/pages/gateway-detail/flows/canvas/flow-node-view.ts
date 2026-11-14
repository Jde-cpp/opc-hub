import { Component, computed, input, output } from '@angular/core';
import { MatIconModule } from '@angular/material/icon';
import { MatTooltipModule } from '@angular/material/tooltip';
import { FFlowModule } from '@foblex/flow';
import { flowNodeType, flowOutputs } from '../../../../model/flow-nodes';
import { CanvasNode, FlowNodeDecor, inId, outId } from './flow-canvas-state';

//A node on the canvas, drawn by its type (#455):  its category's colour, its icon, its name or its type, an input port where
//the type takes one and an output port per output it has, a switch's a rule;  a warning where its props do not read, and an
//inject's button.  The canvas puts fNode on this host, and the ports here find it.  The library reads a node's text to name
//it, an icon's ligature included, so the host carries an aria-label.  On a view of the flow's activity or history (#456),
//`live` adds its status as a badge under it, Node-RED's dot or ring in its fill colour with its text, its drops and errors as
//counters, the error colour once it has erred, and the path colour where a message selected passed;  `linked` draws a link's
//port on a `link out` and a `link in`, for the dashed edge the view draws between them;  `locked` takes no wire.
@Component({
	selector: 'flow-node-view',
	template: `
		@if( type()?.input || (linked() && node().type=='link in') ){
			<div fConnector fConnectorType="target" [fConnectorId]="inPort()" [fConnectorMultiple]="true" fConnectorConnectableSide="left" [fConnectorDisabled]="locked()" class="port in" [class.link]="!type()?.input"></div>
		}
		<mat-icon aria-hidden="true">{{ type()?.icon ?? 'help' }}</mat-icon>
		<span class="label">
			<span class="name">{{ node().name || node().type }}</span>
			@if( node().name ){ <span class="type">{{ node().type }}</span> }
		</span>
		@if( fault(); as f ){
			<mat-icon class="fault" [matTooltip]="f" aria-hidden="true">warning</mat-icon>
		}
		@if( live()?.error; as e ){
			<mat-icon class="erred" [matTooltip]="e" aria-hidden="true">error</mat-icon>
		}
		@if( node().type=='inject' ){
			<button type="button" class="inject" fDragBlocker [disabled]="!injectable()" (click)="inject.emit()" (keydown.enter)="$event.stopPropagation()" (keydown.space)="$event.stopPropagation()"
				[attr.aria-label]="'Inject ' + (node().name || 'a message')" [matTooltip]="injectable() ? 'Send its message' : 'Sends its message once the flow is deployed and enabled'"><mat-icon aria-hidden="true">play_arrow</mat-icon></button>
		}
		<div class="outs">
			@for( port of outPorts(); track port ){
				<div fConnector fConnectorType="source" [fConnectorId]="port" [fConnectorMultiple]="true" fConnectorConnectableSide="right" [fConnectorDisabled]="locked()" class="port out"></div>
			}
			@if( linked() && node().type=='link out' ){
				<div fConnector fConnectorType="source" [fConnectorId]="linkPort()" [fConnectorMultiple]="true" fConnectorConnectableSide="right" [fConnectorDisabled]="true" class="port out link"></div>
			}
		</div>
		@if( badge(); as b ){
			<div class="badge" [matTooltip]="b.tip">
				@if( b.status ){ <span class="status {{ b.status.fill }}" [class.ring]="b.status.shape=='ring'"></span><span class="text">{{ b.status.text }}</span> }
				@if( b.drops ){ <span class="figure drops">{{ b.drops }} dropped</span> }
				@if( b.errors ){ <span class="figure errors">{{ b.errors }} {{ b.errors==1 ? 'error' : 'errors' }}</span> }
			</div>
		}`,
	host: {
		'[class.source]': "type()?.category=='source'", '[class.sink]': "type()?.category=='sink'", '[class.unknown]': '!type()',
		'[class.faulty]': '!!fault()', '[class.refused]': 'refused()', '[class.tall]': 'outPorts().length>2',
		'[class.erred]': '!!live()?.error', '[class.own]': "live()?.lit=='own'", '[class.kin]': "live()?.lit=='kin'", '[class.fired]': "live()?.lit=='fired'",
		'[style.--flow-ports]': 'outPorts().length', '[attr.aria-label]': 'label()'
	},
	imports: [FFlowModule, MatIconModule, MatTooltipModule]
})
export class FlowNodeView{
	node = input.required<CanvasNode>();
	fault = input<string>();//what is wrong with its props, or its wires
	refused = input( false );//the node the gateway's refusal names
	injectable = input( false );
	live = input<FlowNodeDecor>();
	linked = input( false );
	locked = input( false );
	inject = output<void>();
	type = computed( ()=>flowNodeType( this.node().type ) );
	inPort = computed( ()=>inId( this.node().id ) );
	linkPort = computed( ()=>outId( this.node().id, 0 ) );
	outPorts = computed( ()=>Array.from( {length: flowOutputs( this.node() )}, ( _, port )=>outId( this.node().id, port ) ) );
	badge = computed( ()=>{
		const live = this.live();
		if( !live )
			return undefined;
		const status = live.status?.text || live.status?.fill ? live.status : undefined, drops = live.counts?.drops ?? 0, errors = live.counts?.errors ?? 0;
		if( !status && !drops && !errors )
			return undefined;
		const counts = live.counts;
		const tip = counts ? `${counts.received} received, ${counts.sent.length ? counts.sent.join( ' / ' ) : 0} sent, ${counts.drops} dropped, ${counts.errors} ${counts.errors==1 ? 'error' : 'errors'}` : status?.text ?? '';
		return { status, drops, errors, tip };
	} );
	label = computed( ()=>{
		const node = this.node(), fault = this.fault(), live = this.live();
		return `${node.name ? `${node.name}, ${node.type}` : node.type}${fault ? `.  ${fault}` : ''}${live?.status?.text ? `.  ${live.status.text}` : ''}${live?.error ? `.  ${live.error}` : ''}`;
	} );
}