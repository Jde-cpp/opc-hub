import { Component, effect, inject, model, OnDestroy, signal, untracked, viewChild } from '@angular/core';
import { MatCheckbox, MatCheckboxChange } from "@angular/material/checkbox";
import { MatSortModule, Sort } from "@angular/material/sort";
import { MatTable, MatTableModule } from "@angular/material/table";
import { Latest, ProfileStore } from "jde-spa";
import { verify, EnumKeysPipe, SnackbarService } from "jde-framework";
import { Permission, Rights } from "../../model/permission";
import { ACCESS_SERVICE } from "../../services/access-service";
import { Resource } from "../../model/resource";

@Component({
		selector: 'permission-table',
		templateUrl: 'permission-table.html',
		styleUrls: ['permission-table.scss'],
		imports: [MatTableModule, MatCheckbox, EnumKeysPipe, MatSortModule],
})
export class PermissionTable implements OnDestroy{
	#cnsle = inject( SnackbarService );
	#accessService = inject( ACCESS_SERVICE );

	//Built from an effect rather than ngOnInit because the HOST outlives the row:  user-detail and role-detail keep this
	//table alive across ':slug', handing it the next row's permissions with no re-creation, and a one-shot ngOnInit left
	//the previous row's checks on screen - which `toggle` would then have saved onto the new row.
	constructor(){
		effect( ()=>{
			const permissions = this.permissions();
			if( permissions===this.#published )
				return;//one of our own toggles echoing back;  rebuilding would throw the row objects the table is editing away
			untracked( ()=>this.#build(permissions) );
		});
	}

	async #build( permissions:Permission[] ):Promise<void>{
		const build = this.#latest.next();
		this.isLoading.set( true );
		try{
			if( !this.profile ){//the sort is the user's, not the row's - loaded once and kept across rows
				this.profile = await this.profileStore.load<Profile>( 'permissionTable', PermissionTable.defaultProfile );
				this.#sort = {...(this.profile.sort ?? PermissionTable.defaultProfile.sort)};//copy: ProfileStore.load hands back the default OBJECT itself when nothing is stored, and that default is a static shared by every instance
			}
			const resources = await this.#accessService.loadResources();
			if( !this.#latest.is(build) )
				return;
			const rows:Permission[] = [];
			for( const resource of resources ){ //.filter(x=>x.allowed!=Rights.None)
				let permission = permissions.find( x=>x.resource?.id==resource.id );
				if( permission ){
					permission.resource = resources.find( x=>x.id==resource.id )!;
					verify( permission.resource, `Resource not found: ${resource.id}` );
				}else
					permission =  new Permission( {resource: new Resource( resource )} );
				rows.push( permission );
			}
			this.availablePermissions = rows;//replaced, never appended: a second row would otherwise stack its resources onto the first's
			if( this.#sort.direction ){//'' is MatSort's unsorted state - leave the rows in load order rather than sorting descending
				for( const col of this.#sort.active.split(",").filter(x=>this.displayedColumnNames.includes(x)).reverse() )
					this.#applySort( {active:col, direction: this.#sort.direction} );//not sortData(): that records the user's choice, and the multi-key loop would leave #sort holding only the last column
			}
			this.isLoading.set( false );
		}
		catch( e ){
			this.#cnsle.exception( "Could not load the permissions.", e );//the load used to be an unawaited ngOnInit, so a failure left the spinner up with nothing said
		}
	}
	ngOnDestroy(){
		//the component is already going away, so a snackbar would follow the user to the next page:  warn, but never leave the
		//rejection unhandled - save() reports failures now instead of swallowing them.
		this.profileStore.save<Profile>( 'permissionTable', { sort: this.sort } )
			.catch( e=>console.warn("Could not save the permission table settings.", e) );
	}
	sortData($event:Sort){
		this.#sort = $event;//remember what the user clicked; ngOnDestroy used to persist the hardcoded getter instead, so the choice was thrown away
		this.#applySort( $event );
	}
	#applySort($event:Sort){
		this.availablePermissions = this.availablePermissions.sort((a:Permission,b:Permission)=>{
			let y:number;
			if( ["schema", "resource", "deleted", "slug"].includes($event.active) ){
				const col:string = $event.active=="resource" ? "name" : $event.active;
				y = `${(a.resource as any)[col] ?? ''}`.localeCompare( `${(b.resource as any)[col] ?? ''}` );//stringify+default — `deleted` is a Date|undefined
			}else{
				const right = Rights[$event.active as keyof typeof Rights];//rights columns are named by Right ("Read"…) — `Rights[+active]` was Rights[NaN], a sorting no-op
				let value = (x:Permission)=>{ return this.isAllowed(x, right) ? 1 : this.isDenied(x, right) ? -1 : 0; };
				y = value(b) - value(a);
			}
			return $event.direction=="asc" ? y : -y;
		});
		this.table()?.renderRows();
	}

	toggle( $event:MatCheckboxChange|MouseEvent, permission: Permission, rights:Rights ):void{
		if( rights==Rights.All )
			rights = permission.resource.availableRights;
		if( this.isAllowed(permission, rights) ){//allowed->denied
			permission.allowed &= ~rights;
			permission.denied |= rights;
		}else if( this.isDenied( permission, rights ) ){//denied->none
			permission.denied &= ~rights;
		}else{//none->allowed
			if( rights==Rights.None )
				permission.allowed = Rights.None;
			else
				permission.allowed |= rights;
		}
		let permissions = this.permissions();
		let existing = permissions.find( x=>x.resource?.id==permission.resource.id );
		//remembered, then published:  the rebuild effect tells "the host handed us a new row" from "we just published a
		//toggle" by identity, and these rows are the very objects the table is editing.
		this.#published = existing ? [...permissions] : [...permissions, permission];
		this.permissions.set( this.#published );
	}

	isAllowed( permission: Permission, rights:Rights ):boolean{
		let allowed = (permission.allowed ?? 0);
		if( rights==Rights.All )
			rights = permission.resource.availableRights;
		let y = ( rights==Rights.None && !allowed && !permission.denied )
			|| (rights!=Rights.None && (allowed & rights)==rights);
		return y;
	}

	isDenied( permission: Permission, rights:Rights ):boolean{
		let denied = (permission.denied ?? 0);
		if( rights==Rights.All )
			rights = permission.resource.availableRights;
		return rights!=Rights.None && (denied & rights)==rights;
	}
	isAvailable( permission: Permission, rights:Rights ):boolean{
		return rights==Rights.None || (permission.resource.availableRights & rights)!=0;
	}

	profile!:Profile;
	#published:Permission[]|undefined;
	#latest = new Latest();
	static readonly defaultProfile:Profile = { sort: { active: "schema,resource", direction: "asc" } };
	permissions=model.required<Permission[]>();
	availablePermissions:Permission[] = [];
	rights = Rights;
	get sort():Sort{ return this.#sort; } #sort:Sort = {...PermissionTable.defaultProfile.sort};
	table = viewChild<MatTable<Permission>>( 'mainTable' );
	isLoading = signal<boolean>( true );
	get displayedColumnNames():string[]{
		let y = ["schema", "resource", "deleted"];
		for( const kv of EnumKeysPipe.prototype.transform.call(this, Rights) )
			y.push( kv.value );
		return y;
	}
	profileStore = inject(ProfileStore);
}
type Profile = { sort:Sort; }