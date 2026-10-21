import { TestBed } from '@angular/core/testing';
import { ELogLevel } from 'jde-proto/Log';
import { SeverityPicker } from './severity-picker';
import { stable } from 'jde-spa/testing';

//review3 C6: the template's loop moved from *ngFor to @for.  A render test, because the conversion is only compile-checked
//otherwise - a wrong `track` shows up as duplicated or missing rows, not as a type error.
describe( 'SeverityPicker renders every level', ()=>{
	it( 'lists one option per level', async ()=>{
		TestBed.configureTestingModule({});
		const fixture = TestBed.createComponent( SeverityPicker );
		fixture.componentRef.setInput( 'level', ELogLevel.Warning );
		fixture.detectChanges();
		fixture.nativeElement.querySelector( '.mat-mdc-select-trigger' ).click();
		await stable( fixture, 1 );
		const options = [...document.querySelectorAll( 'mat-option' )].map( o=>o.textContent!.trim() );//the panel is in the cdk overlay, outside the fixture
		expect( options ).toEqual( ['Trace','Debug','Information','Warning','Error','Critical','None'] );
	} );
} );

//review3 C7: `level` became a model().  The old @Input setter emitted levelChange on EVERY write after the first, so a
//parent-driven update bounced straight back at the parent.
describe( 'SeverityPicker.level emits once, and only for the user', ()=>{
	const create = ()=>{
		TestBed.configureTestingModule({});
		const fixture = TestBed.createComponent( SeverityPicker );
		fixture.componentRef.setInput( 'level', ELogLevel.Warning );
		fixture.detectChanges();
		const emitted:ELogLevel[] = [];
		fixture.componentInstance.level.subscribe( (l:ELogLevel)=>emitted.push(l) );
		return { fixture, emitted };
	};

	it( 'emits exactly once when the user picks a level', ()=>{
		const { fixture, emitted } = create();
		fixture.componentInstance.onSelectionChange( ELogLevel.Error );
		expect( emitted ).toEqual( [ELogLevel.Error] );
	} );

	it( 'does not emit when the parent writes the input', ()=>{
		const { fixture, emitted } = create();
		fixture.componentRef.setInput( 'level', ELogLevel.Critical );
		fixture.detectChanges();
		expect( emitted ).toEqual( [] );//the bounce-back the old setter produced
		expect( fixture.componentInstance.level() ).toBe( ELogLevel.Critical );
	} );
} );