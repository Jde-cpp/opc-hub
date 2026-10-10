import { Component, computed, effect, input, model, signal, untracked } from '@angular/core';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatInputModule } from '@angular/material/input';
import { MatSelectModule } from '@angular/material/select';
import { MatTooltipModule } from '@angular/material/tooltip';
import { HistAggregate, HistAggregation } from '../../../model/hist';

export type IntervalUnit = 's'|'m'|'h'|'d';
export const intervalUnits:{ value:IntervalUnit; name:string; ms:number }[] = [
	{ value: 's', name: 'seconds', ms: 1000 }, { value: 'm', name: 'minutes', ms: 60_000 }, { value: 'h', name: 'hours', ms: 3_600_000 }, { value: 'd', name: 'days', ms: 86_400_000 }
];
//an interval in the largest unit that holds it whole:  90 s stays seconds, 120 s is 2 minutes
export function fromMs( interval:number ):{ amount:number; unit:IntervalUnit }{
	const unit = [...intervalUnits].reverse().find( u=>interval%u.ms==0 ) ?? intervalUnits[0];
	return { amount: interval/unit.ms, unit: unit.value };
}

//The aggregate picker:  the raw values, or one of the aggregates the server lists over intervals of a length typed here, in
//seconds, minutes, hours or days (spec *Reads*).  `aggregation` is what the tab reads by:  none for the raw values, else the
//aggregate's browse name and the interval in milliseconds, set only when the interval is a positive number - an emptied or zero
//field leaves the last read standing and is marked.
@Component({
	selector: 'hist-aggregate-picker',
	templateUrl: './hist-aggregate-picker.html',
	styleUrls: ['./hist-aggregate-picker.scss'],
	imports: [MatFormFieldModule, MatInputModule, MatSelectModule, MatTooltipModule]
})
export class HistAggregatePicker{
	constructor(){
		//the fields follow a value set from outside, the tab's when the picker comes back after Modifications;  one the picker
		//set itself is in them already - 120 seconds stays as typed rather than turning into 2 minutes, and a field emptied
		//stays emptied, marked, rather than snapping back to the last read
		effect( ()=>{
			const a = this.aggregation();
			untracked( ()=>{
				if( a===this.#own )
					return;
				this.#own = a;
				this.aggregate.set( a?.aggregate ?? '' );
				if( a ){
					const {amount, unit} = fromMs( a.interval );
					this.amount.set( String( amount ) );
					this.unit.set( unit );
				}
			} );
		} );
	}
	aggregates = input.required<HistAggregate[]>();
	disabled = input( false );
	aggregation = model<HistAggregation|undefined>( undefined );
	aggregate = signal( '' );//the browse name picked;  '' is the raw values
	amount = signal( '1' );
	unit = signal<IntervalUnit>( 'm' );
	interval = computed<number|undefined>( ()=>{ const n = Number( this.amount() ); return this.amount().trim() && Number.isFinite( n ) && n>0 ? n*HistAggregatePicker.ms( this.unit() ) : undefined; } );
	invalid = computed( ()=>!!this.aggregate() && this.interval()==undefined );
	readonly units = intervalUnits;
	onAggregate( browse:string ){ this.aggregate.set( browse ); this.#emit(); }
	onAmount( text:string ){ this.amount.set( text ); this.#emit(); }
	onUnit( unit:IntervalUnit ){ this.unit.set( unit ); this.#emit(); }
	#emit(){
		const aggregate = this.aggregate(), interval = this.interval();
		if( aggregate && interval==undefined )
			return;
		this.#own = aggregate ? {aggregate, interval: interval!} : undefined;
		this.aggregation.set( this.#own );
	}
	#own:HistAggregation|undefined;//the value the picker set last, which its fields already show
	static ms( unit:IntervalUnit ):number{ return intervalUnits.find( u=>u.value==unit )!.ms; }
}