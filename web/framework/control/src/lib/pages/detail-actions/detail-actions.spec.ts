import { Component, signal } from '@angular/core';
import { TestBed } from '@angular/core/testing';
import { DetailActions } from './detail-actions';

//reviews/angular-refactor.md A3:  six detail pages each carried a copy of this row, two of them "<another page>'s row, verbatim".
type Row = { changed?:boolean, isNew?:boolean, isDeleted?:boolean, canPurge?:boolean };
const page = ( row:Row )=>({
	isChanged: signal( row.changed ?? false ),
	isNew: row.isNew ?? false,
	isDeleted: row.isDeleted ?? false,
	onSubmitClick: vi.fn(),
	onCancelClick: vi.fn(),
	onDeleteClick: vi.fn(),
	onPurgeClick: vi.fn()
});

@Component({
	imports: [DetailActions],
	template: `<detail-actions [page]="page" [canPurge]="canPurge"><button id="extra">Test Connection</button></detail-actions>`
})
class Host{
	page:any;
	canPurge = true;
}

describe( 'DetailActions', ()=>{
	const create = ( row:Row = {} )=>{
		const fixture = TestBed.createComponent( Host );
		fixture.componentInstance.page = page( row );
		fixture.componentInstance.canPurge = row.canPurge ?? true;
		fixture.detectChanges();
		const buttons = [...(fixture.nativeElement as HTMLElement).querySelectorAll( 'detail-actions > button' )] as HTMLButtonElement[];
		return { page: fixture.componentInstance.page as ReturnType<typeof page>, buttons, labels: buttons.map( b=>b.textContent!.replace(b.querySelector('mat-icon')?.textContent ?? "", "").trim() ) };//the label, without the icon's ligature
	};
	const far = ( b:HTMLButtonElement )=>b.classList.contains( "far" );

	it( 'drives the page it sits on', ()=>{
		const { page, buttons } = create( {changed: true} );
		for( const b of buttons )
			b.click();
		expect( page.onSubmitClick ).toHaveBeenCalledTimes( 1 );
		expect( page.onCancelClick ).toHaveBeenCalledTimes( 1 );
		expect( page.onDeleteClick ).toHaveBeenCalledTimes( 1 );
		expect( page.onPurgeClick ).not.toHaveBeenCalled();
	} );

	it( 'holds Save until the form changes, and Delete until the row is saved', ()=>{
		expect( create().buttons[0].disabled ).toBe( true );
		expect( create( {changed: true} ).buttons[0].disabled ).toBe( false );
		const { buttons } = create( {isNew: true} );
		expect( buttons.at(-1)!.disabled ).toBe( true );
	} );

	it( "puts the page's own control between Cancel and the trash can, and the trash can at the far side", ()=>{
		const { buttons, labels } = create();
		expect( labels ).toEqual( ["Save", "Cancel", "Test Connection", "Delete"] );
		expect( buttons.map(far) ).toEqual( [false, false, false, true] );
	} );

	it( 'offers Purge beside Restore on a deleted row, and takes the far side from it', ()=>{
		const { buttons, labels } = create( {isDeleted: true} );
		expect( labels.slice(-2) ).toEqual( ["Purge", "Restore"] );
		expect( buttons.slice(-2).map(far) ).toEqual( [true, false] );
		expect( buttons.at(-2)!.classList.contains("purge") ).toBe( true );
	} );

	it( 'offers no Purge where the page has none', ()=>{
		const { buttons, labels } = create( {isDeleted: true, canPurge: false} );
		expect( labels.at(-1) ).toBe( "Restore" );
		expect( labels ).not.toContain( "Purge" );
		expect( far(buttons.at(-1)!) ).toBe( true );
	} );
} );