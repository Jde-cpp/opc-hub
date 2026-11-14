import { Component, input, output } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MatCheckboxModule } from '@angular/material/checkbox';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatIconModule } from '@angular/material/icon';
import { MatInputModule } from '@angular/material/input';
import { MatSelectModule } from '@angular/material/select';
import { MatTooltipModule } from '@angular/material/tooltip';
import { FlowGraph } from '../../../../model/flow';
import { FlowField, flowFieldText, flowFieldValue, flowPruneRule, flowWithField } from '../../../../model/flow-nodes';

type Props = Record<string,unknown>;

//A node's props as a form (#455), from its type's fields (flow-nodes.ts):  each field an input of its kind, a group its own
//fields under a legend and a rule list a card a rule, each this component again.  A field left blank takes its key away, so the
//node takes its default, which the placeholder names;  the gateway refuses an empty text where a key is present.  A change is
//told as the whole object, a new one, once its input is left, so the canvas takes an edit as one step of its history.
@Component({
	selector: 'flow-fields',
	templateUrl: './flow-fields.html',
	styleUrl: './flow-fields.scss',
	imports: [MatButtonModule, MatCheckboxModule, MatFormFieldModule, MatIconModule, MatInputModule, MatSelectModule, MatTooltipModule]
})
export class FlowFields{
	fields = input.required<FlowField[]>();
	value = input.required<Props>();
	graph = input.required<FlowGraph>();
	self = input<string>();//the node the form is of, which a scope does not offer
	valueChange = output<Props>();

	text( f:FlowField ):string{ return flowFieldText( f, this.value()[f.key] ); }
	placeholder( f:FlowField ):string{ return f.dflt===undefined ? '' : typeof f.dflt=='string' ? f.dflt : JSON.stringify( f.dflt ); }
	setText( f:FlowField, text:string ):void{ this.#set( f.key, flowFieldValue( f, text, this.value()[f.key] ) ); }
	checked( f:FlowField ):boolean{ const v = this.value()[f.key]; return typeof v=='boolean' ? v : f.dflt===true; }
	//a flag at its default is left out, as a blank field is
	setFlag( f:FlowField, checked:boolean ):void{ this.#set( f.key, f.dflt!==undefined && checked===f.dflt ? undefined : checked ); }
	choice( f:FlowField ):string{ const v = this.value()[f.key]; return typeof v=='string' ? v : ''; }
	setChoice( f:FlowField, value:string ):void{ this.#set( f.key, value || undefined ); }
	//what a choice left unset means, none for one that must be made
	unset( f:FlowField ):string|undefined{ return f.blank ?? ( f.dflt!==undefined ? `${this.label( f, <string>f.dflt )} (default)` : f.required ? undefined : 'None' ); }
	label( f:FlowField, option:string ):string{ return f.labels?.[option] ?? option; }
	list( f:FlowField ):string[]{ const v = this.value()[f.key]; return Array.isArray( v ) ? v.filter( x=>typeof x=='string' ) : []; }
	setList( f:FlowField, items:string[] ):void{ this.#set( f.key, items.length ? items : undefined ); }
	nodes(){ return this.graph().nodes.filter( n=>n.id!=this.self() ); }
	group( f:FlowField ):Props{ const v = this.value()[f.key]; return v && typeof v=='object' && !Array.isArray( v ) ? <Props>v : {}; }
	//a group with nothing set is left out
	setGroup( f:FlowField, value:Props ):void{ this.#set( f.key, Object.keys( value ).length ? value : undefined ); }
	rules( f:FlowField ):Props[]{ const v = this.value()[f.key]; return Array.isArray( v ) ? v.filter( x=>x && typeof x=='object' ) : []; }
	setRule( f:FlowField, index:number, rule:Props ):void{ this.#set( f.key, this.rules( f ).map( ( r, i )=>i==index ? flowPruneRule( rule, f.fields! ) : r ) ); }
	addRule( f:FlowField ):void{
		const first = f.fields!( {} )[0];
		this.#set( f.key, [...this.rules( f ), first?.options ? {[first.key]: first.options[0]} : {}] );
	}
	removeRule( f:FlowField, index:number ):void{ this.#set( f.key, this.rules( f ).filter( ( _, i )=>i!=index ) ); }
	moveRule( f:FlowField, index:number, by:number ):void{
		const rules = [...this.rules( f )], to = index+by;
		if( to<0 || to>=rules.length )
			return;
		[rules[index], rules[to]] = [rules[to], rules[index]];
		this.#set( f.key, rules );
	}
	#set( key:string, value:unknown ):void{ this.valueChange.emit( flowWithField( this.value(), key, value ) ); }

	//Tab indents in a body, as an editor's does;  Escape first lets the next Tab leave the field, so the keyboard is never held.
	#leaving = false;
	onCodeKey( event:KeyboardEvent ):void{
		if( event.key=='Escape' ){
			this.#leaving = true;
			return;
		}
		if( event.key=='Tab' && !event.shiftKey && !event.ctrlKey && !event.altKey && !event.metaKey && !this.#leaving ){
			event.preventDefault();
			const area = <HTMLTextAreaElement>event.target;
			area.setRangeText( '\t', area.selectionStart, area.selectionEnd, 'end' );
		}
		this.#leaving = false;
	}
}