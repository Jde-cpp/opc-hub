import { Permission } from "./permission";
import { cloneClassArray, Mutation, SlugRow } from "jde-framework";
import { User } from "./user";
import { Group } from "./group";
import { Acl } from "./acl";

export type RolePK = number;
export class Role extends SlugRow<Role>{
	constructor( obj:any ){
		super( Role.typeName, obj );
		this.permissions = cloneClassArray( obj.permissionRights ?? obj.permissions, Permission ) ?? [];
		let childRoles = obj.roles ?? obj.childRoles;
		if( childRoles )
			this.childRoles = childRoles.map( (r:any)=>new Role(r) );
		if( obj.acl ){
			this.users = [];
			this.groups = [];
			for( let ac of obj.acl ){
				for( let identity of ac.identities )
				if( identity.isGroup ){
					this.groups.push( new Group(identity) );
				}else
					this.users.push( new User(identity) );
			}
		}
		else{
			this.users = cloneClassArray( obj.users, User );
			this.groups = cloneClassArray( obj.groups, Group );
		}
	}
	override equals( obj:Partial<Role> ):boolean{
		const role = <Role><unknown>obj;
		return super.equals(obj) && JSON.stringify(this.permissions)==JSON.stringify(role.permissions);
	}
	override mutation( original:Role ):Mutation[]{
		let propertyies = super.mutation( original );
		const permissions = Permission.roleMutations( original.id, this.permissions, original.permissions );
		const childRoles = super.childMutations( this, original.childRoles ?? [], this.childRoles, {role:{}} );
		const member = ( id:number )=>({identity: {id}, role: {id: this.id}});
		const groups = Acl.mutations( original.groups, this.groups, member );
		const users = Acl.mutations( original.users, this.users, member );
		return [...propertyies, ...permissions, ...childRoles, ...groups, ...users];
	}

	childRoles!: Role[];
	groups: Group[];
	permissions: Permission[];
	get properties():Partial<Role>{ let properties:Partial<Role> = new Role(this); properties.childRoles=undefined; properties.permissions=undefined; return properties; }
	override get collectionName():string{ return "roles"; }
	users: User[];
	static typeName = "Role";
}