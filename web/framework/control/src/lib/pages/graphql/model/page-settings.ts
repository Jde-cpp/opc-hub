import { ProfileStore } from "jde-spa";
import { View, ViewFieldSettings } from "../../../model/ql/view";
import { TableSchema } from '../../../model/ql/schema/table-schema';
import type { TableSettings } from '../../../services/ql-list-resolver';//type-only: ql-list-resolver imports this file, and a value import would close the cycle

export class PageProfile{
	constructor( args?:PageProfile ){
		if( args ){
			this.currentViewIndex = args.currentViewIndex;
			//copy the ARRAY: every caller builds `new PageProfile(current)` as a private working copy, mutates it through
			//upsertView/updateView and only then swaps it in via refresh() - sharing the array wrote those edits straight
			//back into the profile still on screen.  Shallow on purpose: the View instances stay identical, which the logs
			//view picker and the indexOf/currentViewIndex bookkeeping rely on.
			this.views = [...args.views];
		}
	}
	upsertView( view:View, collectionName:string, profileStore:ProfileStore ){
		let existing = this.views.findIndex( v=>v.name==view.name && view.type==v.type );
		if( existing>=0 ){
			this.views[existing] = new View( view );
			this.currentViewIndex = existing;
		}
		else{
			this.views.push( new View(view) );
			this.currentViewIndex = this.views.length - 1;
		}
	}
	async loadViews( collectionName:string, profileStore:ProfileStore, schema:TableSchema, defaultView:View ){
		const views = await profileStore.loadClassArray<View>( `qlList/${collectionName}/views`, View, schema, defaultView );
		this.views.push( ...views );
	}
	async removeView( viewName:string, collectionName:string, profileStore:ProfileStore, defaultSettings:TableSettings|undefined ){
		this.views = this.views.filter( v=>!(v.name==viewName && (v.isUser || v.isAdhoc)) );//the saved view and its unsaved twin - never a system view of the same name (a route can declare one, and only user views are deletable)
		await profileStore.save( `qlList/${collectionName}/views`, this.views.filter(v=>v.isUser).map(v=>v.toJson(defaultSettings)) );
	}
	updateView( view:View ){
		this.views[this.views.findIndex( v=>v.name==view.name && view.type==v.type)] = view;
	}
	currentViewIndex:number=0;
	get view():View{
		if( this.currentViewIndex>=this.views.length ){
			console.error( `Invalid currentViewIndex: ${this.currentViewIndex}` );
			this.currentViewIndex = 0;
		}
		return this.views[this.currentViewIndex];
	}
	views:View[] = [];
}
export class PageSettings{
	constructor( x:any ){
		this.configColumns = x.columns ? [ ...x.columns ] : [];
		this.excludedColumns = x.excludedColumns;
		this.name = x.name;
		this.table = x.table ?? x.id;
	}
	name:string;
	//canAdd/canPurge are not copied here:  QLList reads both straight off the TableSettings, and a second copy
	//on this class only gave a route somewhere else to set them and be ignored
	configColumns:(string|ViewFieldSettings)[];
	excludedColumns:string[];
	table:string;
	get type():string{ return this.table ?? this.name[0].toLowerCase()+this.name.substring(1); }
}
