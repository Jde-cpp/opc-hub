import { Component, computed, input, output } from '@angular/core';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatIconModule } from '@angular/material/icon';
import { MatInputModule } from '@angular/material/input';
import { FlowGraph, FlowNode } from '../../../../model/flow';
import { FlowNodePatch, flowNodeType } from '../../../../model/flow-nodes';
import { FlowFields } from './flow-fields';

//The side form of the node selected (#455), typed by the node's type:  its name, and its props as the type's fields
//(FlowFields), a Function node's body among them, in a plain editor.  What is wrong with it as it stands is said above the
//fields.  A switch's ports are its rules, so a change of its rules says where each port was, and its wires follow.
@Component({
	selector: 'flow-node-form',
	template: `
		<header>
			<mat-icon aria-hidden="true">{{ type()?.icon ?? 'help' }}</mat-icon>
			<div>
				<h3>{{ node().type }}</h3>
				<p>{{ type()?.about ?? 'Not a node type the editor knows:  the gateway would refuse it.' }}</p>
			</div>
		</header>
		@if( fault(); as f ){
			<p class="fault" role="status"><mat-icon aria-hidden="true">warning</mat-icon><span>{{ f }}</span></p>
		}
		<mat-form-field subscriptSizing="dynamic">
			<mat-label>Name</mat-label>
			<input matInput type="text" autocomplete="off" [value]="node().name" (change)="patch.emit( {name: $any($event.target).value.trim()} )" [placeholder]="node().type">
		</mat-form-field>
		<flow-fields [fields]="fields()" [value]="node().props" [graph]="graph()" [self]="node().id" (valueChange)="setProps($event)"/>
		<p class="id">Id <span>{{ node().id }}</span></p>`,
	styles: `
		:host{ display: flex; flex-direction: column; gap: 10px; }
		header{
			display: flex; gap: 10px; align-items: flex-start;
			mat-icon{ flex: none; margin-top: 2px; color: var(--mat-sys-on-surface-variant); }
			h3{ margin: 0; font: var(--mat-sys-title-small); }
			p{ margin: 2px 0 0; color: var(--mat-sys-on-surface-variant); font: var(--mat-sys-body-small); }
		}
		mat-form-field{ width: 100%; }
		.fault{
			display: flex; gap: 8px; align-items: flex-start; margin: 0; padding: 8px 10px; border-radius: 8px;
			color: var(--mat-sys-on-error-container); background: var(--mat-sys-error-container); font: var(--mat-sys-body-small);
			mat-icon{ flex: none; font-size: 18px; width: 18px; height: 18px; }
		}
		.id{
			margin: 0; color: var(--mat-sys-on-surface-variant); font: var(--mat-sys-body-small);
			span{ font-family: 'Roboto Mono', monospace; user-select: all; }
		}`,
	imports: [MatFormFieldModule, MatIconModule, MatInputModule, FlowFields]
})
export class FlowNodeForm{
	node = input.required<FlowNode>();
	graph = input.required<FlowGraph>();
	fault = input<string>();
	patch = output<FlowNodePatch>();
	type = computed( ()=>flowNodeType( this.node().type ) );
	fields = computed( ()=>this.type()?.fields( this.node().props ) ?? [] );

	setProps( props:Record<string,unknown> ):void{
		const was = this.node().props['rules'], now = props['rules'];
		if( this.node().type!='switch' || !Array.isArray( was ) || !Array.isArray( now ) || was===now )
			return this.patch.emit( {props} );
		//a rule the change left as it was is the same object;  one edited keeps its place
		this.patch.emit( {props, ports: now.map( ( rule, i )=>was.includes( rule ) ? was.indexOf( rule ) : now.length==was.length ? i : -1 )} );
	}
}