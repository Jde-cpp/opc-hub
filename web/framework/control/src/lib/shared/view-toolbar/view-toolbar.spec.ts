import { Component, signal } from '@angular/core';
import { TestBed } from '@angular/core/testing';
import { LocalPrefs, ProfileStore } from 'jde-spa';
import { View, ViewType } from '../../model/ql/view';
import { ViewSet } from '../../model/ql/view-set';
import { ViewToolbar } from './view-toolbar';
import { screenSize } from 'jde-spa/testing';
import { schemaOf } from 'jde-framework/testing';

//reviews/angular-refactor.md A2:  QLList, LogDetail and NodeChildren each carried a copy of this toolbar and its stylesheet.
const view = ( name:string|undefined, type=ViewType.System )=>{ const v = View.fromConfig( {configColumns: [], sort: []}, schemaOf("Thing") ); v.name = name; v.type = type; return v; };

@Component({
	imports: [ViewToolbar],
	template: `<view-toolbar [views]="views" [refreshing]="refreshing()" [count]="count()" (viewChange)="changed.push($event)" (edit)="edits=edits+1" (refresh)="refreshes=refreshes+1">
		<button left id="add">Add</button>
		<span right id="extra">extra</span>
	</view-toolbar>`
})
class Host{
	views = new ViewSet( {type: View, key: "things", profileStore: {} as ProfileStore, prefs: new LocalPrefs()} );
	refreshing = signal( false );
	count = signal<number|undefined>( 3 );
	changed:number[] = [];
	edits = 0;
	refreshes = 0;
}

describe( 'ViewToolbar', ()=>{
	const create = ( width:'wide'|'extra-small'='wide' )=>{
		TestBed.configureTestingModule( {providers: [screenSize( width )]} );
		const fixture = TestBed.createComponent( Host );
		fixture.componentInstance.views.reset( [view( undefined ), view( "Users" ), view( "Users", ViewType.Adhoc )], 2 );
		fixture.detectChanges();
		return fixture;
	};
	const element = ( fixture:{nativeElement:HTMLElement} )=>fixture.nativeElement;
	const toggles = ( fixture:{nativeElement:HTMLElement} )=>[...element( fixture ).querySelectorAll( 'mat-button-toggle' )] as HTMLElement[];
	const rowCount = ( fixture:{nativeElement:HTMLElement} )=>element( fixture ).querySelector( '.row-count' )?.textContent!.trim();

	it( 'lists the views, the unnamed one as Default and the twin as edited, on the current one', ()=>{
		const fixture = create();
		expect( toggles(fixture).map( t=>t.textContent!.replace(/\s+/g, " ").trim() ) ).toEqual( ["Default", "Users", "Users (edited)"] );
		expect( toggles(fixture).map( t=>t.classList.contains("mat-button-toggle-checked") ) ).toEqual( [false, false, true] );
	} );

	it( 'hands a view click, Edit and Refresh to the host', ()=>{
		const fixture = create();
		toggles( fixture )[1].querySelector( 'button' )!.click();
		element( fixture ).querySelector<HTMLButtonElement>( 'button[aria-label="Edit view"]' )!.click();
		element( fixture ).querySelector<HTMLButtonElement>( 'button[aria-label="Refresh"]' )!.click();
		const host = fixture.componentInstance;
		expect( host.changed ).toEqual( [1] );
		expect( host.edits ).toBe( 1 );
		expect( host.refreshes ).toBe( 1 );
	} );

	it( 'holds Refresh and runs the bar while a reload is out', ()=>{
		const fixture = create();
		const refresh = element( fixture ).querySelector<HTMLButtonElement>( 'button[aria-label="Refresh"]' )!;
		const bar = element( fixture ).querySelector( 'mat-progress-bar' )!;
		expect( refresh.disabled ).toBe( false );
		expect( bar.classList.contains("mdc-linear-progress--indeterminate") ).toBe( false );
		fixture.componentInstance.refreshing.set( true );
		fixture.detectChanges();
		expect( refresh.disabled ).toBe( true );
		expect( bar.classList.contains("mdc-linear-progress--indeterminate") ).toBe( true );
	} );

	it( 'counts the rows, and says nothing where the host gives no count', ()=>{
		const fixture = create();
		expect( rowCount(fixture) ).toBe( "3 rows" );
		fixture.componentInstance.count.set( 1 );
		fixture.detectChanges();
		expect( rowCount(fixture) ).toBe( "1 row" );
		fixture.componentInstance.count.set( undefined );
		fixture.detectChanges();
		expect( rowCount(fixture) ).toBeUndefined();
	} );

	it( "puts the host's controls left, and right ahead of the count", ()=>{
		const fixture = create();
		expect( element( fixture ).querySelector( '.bar-left #add' ) ).not.toBeNull();
		const right = element( fixture ).querySelector( '.bar-right' )!;
		expect( [...right.children].map( c=>c.id || c.className ) ).toEqual( ["extra", "row-count"] );
	} );

	//a phone's centre zone is the second row:  after the right zone in the document, so the tab order runs row by row (#330)
	it( 'puts the centre zone between the others, and after them on a phone', ()=>{
		const zones = ( fixture:{nativeElement:HTMLElement} )=>[...element( fixture ).querySelector( 'mat-toolbar' )!.children].map( c=>c.className );
		expect( zones(create()) ).toEqual( ["bar-left", "bar-center", "bar-right"] );
		TestBed.resetTestingModule();//a second toolbar, at a phone's width
		expect( zones(create( 'extra-small' )) ).toEqual( ["bar-left", "bar-right", "bar-center"] );
	} );
} );