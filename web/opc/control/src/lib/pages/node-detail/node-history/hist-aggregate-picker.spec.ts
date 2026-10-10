if( typeof globalThis.localStorage=="undefined" ){
	const backing = new Map<string,string>();
	(globalThis as any).localStorage = {
		getItem: ( k:string )=>backing.has(k) ? backing.get(k)! : null,
		setItem: ( k:string, v:string )=>{ backing.set(k, String(v)); },
		removeItem: ( k:string )=>{ backing.delete(k); },
		clear: ()=>backing.clear()
	};
}
import { TestBed } from '@angular/core/testing';
import { HistAggregation } from '../../../model/hist';
import { fromMs, HistAggregatePicker } from './hist-aggregate-picker';

const aggregates = [{name: 'Average', browse: 'Average'}, {name: 'Median', browse: 'Median'}];
const open = ( aggregation?:HistAggregation )=>{
	const fixture = TestBed.createComponent( HistAggregatePicker );
	fixture.componentRef.setInput( 'aggregates', aggregates );
	if( aggregation )
		fixture.componentRef.setInput( 'aggregation', aggregation );
	const set:(HistAggregation|undefined)[] = [];
	fixture.componentInstance.aggregation.subscribe( a=>set.push( a ) );
	fixture.detectChanges();
	return { fixture, picker: fixture.componentInstance, set, field: ( cls:string )=>fixture.nativeElement.querySelector( `mat-form-field.${cls}` ) };
};

describe( 'fromMs', ()=>{
	it( 'picks the largest unit that holds the interval whole', ()=>{
		expect( fromMs( 90_000 ) ).toEqual( {amount: 90, unit: 's'} );
		expect( fromMs( 120_000 ) ).toEqual( {amount: 2, unit: 'm'} );
		expect( fromMs( 7_200_000 ) ).toEqual( {amount: 2, unit: 'h'} );
		expect( fromMs( 86_400_000 ) ).toEqual( {amount: 1, unit: 'd'} );
		expect( fromMs( 1500 ) ).toEqual( {amount: 1.5, unit: 's'} );
	} );
} );

describe( 'HistAggregatePicker', ()=>{
	it( 'opens on the raw values with no interval field, and sets the aggregate with its interval in milliseconds', ()=>{
		const p = open();
		expect( p.picker.aggregate() ).toBe( '' );
		expect( p.field( 'amount' ) ).toBeNull();
		p.picker.onAggregate( 'Average' );
		p.fixture.detectChanges();
		expect( p.set ).toEqual( [{aggregate: 'Average', interval: 60_000}] );//a minute by default
		expect( p.field( 'amount' ) ).not.toBeNull();
		p.picker.onAmount( '5' );
		p.picker.onUnit( 'h' );
		expect( p.set.slice( 1 ) ).toEqual( [{aggregate: 'Average', interval: 300_000}, {aggregate: 'Average', interval: 18_000_000}] );
		p.picker.onAggregate( '' );
		p.fixture.detectChanges();
		expect( p.set.at( -1 ) ).toBeUndefined();
		expect( p.field( 'amount' ) ).toBeNull();
	} );
	//an interval that isn't a positive number reads nothing:  the last read stands and the field is marked
	it( 'sets nothing for an empty or zero interval, and marks the field', ()=>{
		const p = open();
		p.picker.onAggregate( 'Median' );
		p.picker.onAmount( '0' );
		p.fixture.detectChanges();
		expect( p.set ).toEqual( [{aggregate: 'Median', interval: 60_000}] );
		expect( p.picker.invalid() ).toBe( true );
		expect( p.field( 'amount' ).classList.contains( 'mat-form-field-invalid' ) ).toBe( true );
		p.picker.onAmount( '' );
		p.picker.onAmount( '-2' );
		p.picker.onAmount( 'x' );
		expect( p.set ).toHaveLength( 1 );
		p.picker.onAmount( '2.5' );
		expect( p.set.at( -1 ) ).toEqual( {aggregate: 'Median', interval: 150_000} );
		expect( p.picker.invalid() ).toBe( false );
	} );
	//the tab keeps the aggregation across Modifications, where the picker is gone;  back in Values the picker shows it again
	it( 'shows a value set from outside in its fields, in the largest unit', ()=>{
		const p = open( {aggregate: 'Median', interval: 7_200_000} );
		expect( p.picker.aggregate() ).toBe( 'Median' );
		expect( p.picker.amount() ).toBe( '2' );
		expect( p.picker.unit() ).toBe( 'h' );
		expect( p.set ).toEqual( [] );//nothing bounced back
		p.picker.onUnit( 's' );//120 seconds of its own stays as typed
		p.picker.onAmount( '120' );
		p.fixture.detectChanges();
		expect( p.picker.amount() ).toBe( '120' );
		expect( p.picker.unit() ).toBe( 's' );
		expect( p.set.at( -1 ) ).toEqual( {aggregate: 'Median', interval: 120_000} );
	} );
} );