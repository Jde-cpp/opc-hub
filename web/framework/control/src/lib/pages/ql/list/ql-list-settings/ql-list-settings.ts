import { Component, OnInit, input, signal, computed, output, ViewChild, OnDestroy } from '@angular/core';
import { CommonModule } from '@angular/common';
import { MatButtonModule } from '@angular/material/button';
import { MatIcon } from '@angular/material/icon';
import { MatInputModule } from '@angular/material/input';
import { MatSelectModule } from '@angular/material/select';
import { MatTabsModule } from '@angular/material/tabs';
import { MatToolbar } from '@angular/material/toolbar';
import { QLListSettingsFilter } from './ql-list-settings-filter/ql-list-settings-filter';
import { QLListSettingsDisplay } from './ql-list-settings-display/ql-list-settings-display';
import { QLListSettingsSort } from './ql-list-settings-sort/ql-list-settings-sort';
import { View, ViewField } from '../../../../model/ql/view';
import { ProfileStore } from 'jde-spa';
import { TableSchema } from '../../../../model/ql/schema/table-schema';

const tabKey = 'qlListSettings';

@Component({
	selector: 'ql-list-settings',//.main-content.mat-drawer-container.my-content
	styleUrls: ['ql-list-settings.scss'],
	templateUrl: './ql-list-settings.html',
	host: {class:'main-content.mat-drawer-container.my-content'},
	imports: [CommonModule, MatButtonModule, MatIcon, MatInputModule, MatSelectModule, MatTabsModule, MatToolbar, QLListSettingsFilter, QLListSettingsDisplay, QLListSettingsSort]
})
export class QLListSettings implements OnInit, OnDestroy{
	ngOnInit(){
		this.name.set( this.originalName() );
	}
	ngOnDestroy(){
		ProfileStore.setTabIndex( tabKey, this.tabIndex() );
	}

	getView( isAdhoc:boolean ):View{
		let view = new View( this.view() );//copy — mutating the profile's own instance made onViewSave's duplicate-check self-match, so views were never promoted to User/persisted
		if( !isAdhoc )
			view.name = this.name();
		let displayCols = this.display.dataSource;
		const fields = displayCols.filter( c=>c.name!="Selector" ).map( c=>new ViewField(c as ViewField) );
		for( let field of this.view().fields.filter(f=>!fields.find(c=>c.name==f.name)) )//the list only offers columns(), so excluded-but-queried fields (id/slug) would be lost
			fields.push( new ViewField(field) );
		view.fields = fields;
		view.showSelector = displayCols[0].displayed;
		view.limit = this.display.pageSize();
		view.fieldFilters = [];
		for( let col of this.filter.dataSource.filter(c=>c.field) )
			view.fieldFilters.push( {field: col.field, filter: View.copyFilter(col.filter)} );//the built Filter was new but re-used col.filter.value, so the emitted view still shared the dialog's live arrays
		view.sort = this.sort.dataSource.filter( s=>s.active ).map( s=>({active: s.active, direction: s.direction}) );
		return view;
	}

	disableSave():boolean{
		return ( this.view().isSystem && this.originalName()==this.name() )
			|| ( this.originalName()==this.name() && !this.view().name )
			|| this.reservedName();
	}
	//A system view edited and saved under another system view's name stayed a System view (onViewSave), so it replaced that view
	//for the session and was never persisted - with Deleted on every list, any list could hit it.
	reservedName = computed( ()=>this.name()!=this.view().name && this.reservedNames().includes(this.name()) );
	//Delete only ever removes a saved user view.  It used to share disableSave(), which left it ENABLED for an adhoc view —
	//and logs' onViewDelete opens with an assertion on the type, so clicking it there threw (via a `debugger;` that freezes
	//the page outright whenever DevTools/automation is attached).
	disableDelete():boolean{ return !this.view().isUser; }
	onNameInput( event:Event ){ this.name.set( (<HTMLInputElement>event.target).value ); }
	onNameFocus( event:FocusEvent ){ (<HTMLInputElement>event.target).select(); }//was `$event.target.select()` in the template, which type-checks only because EventTarget is loose there
	onTabIndexChanged( index:number ){ this.tabIndex.set(index); }
	excludedColumns = input<string[]>([]);
	reservedNames = input<string[]>([]);//the host's system view names
	suggestions = input.required<Record<string,any[]>>();
	schema = input.required<TableSchema>();
	view = input.required<View>();
	columns = input.required<Record<string,string>>();
	originalName = computed( () =>{return this.view().name ?? "<default>" + (this.view().isAdhoc ? "*" : "");} );
	onSave = output<View>();
	onShow = output<View>();
	onDelete = output<View>();
	onCancel = output<void>();

	@ViewChild(QLListSettingsDisplay) display!: QLListSettingsDisplay;
	@ViewChild(QLListSettingsFilter) filter!: QLListSettingsFilter;
	@ViewChild(QLListSettingsSort) sort!: QLListSettingsSort;

	name = signal<string>( null as any );
	tabIndex = signal<number>( ProfileStore.tabIndex(tabKey) );
}
