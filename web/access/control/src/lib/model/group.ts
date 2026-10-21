import { cloneClassArray, Mutation, SlugRow } from "jde-framework";
import { Role } from "./role";
import { Permission } from "./permission";
import { User } from "./user";
import { Acl } from "./acl";

export type GroupPK = number;

export class Group extends SlugRow<Group>{
	constructor( obj:any ){
		super(Group.typeName, obj);
		if( obj.acl )
			({roles: this.roles, permissions: this.permissions} = Acl.split( obj.acl ));
		else{
			this.roles = cloneClassArray( obj.roles, Role );
			this.permissions = cloneClassArray( obj.permissions, Permission );
		}
		if( obj.groupMembers ?? obj.members ){
			this.childGroups = [];
			this.users = [];
			for( let identity of obj.groupMembers ?? obj.members ?? [] ){
				if( identity.isGroup )
					this.childGroups.push( new Group(identity) );
				else
					this.users.push( new User(identity) );
			}
		}
		else{
			this.childGroups = cloneClassArray( obj.childGroups, Group );
			this.users = cloneClassArray( obj.users, User );
		}
	}
	override equals( group:Partial<Group> ):boolean{
		return super.equals(group) && JSON.stringify(this.roles)==JSON.stringify(group.roles);
	}
	override mutation( original:Group ):Mutation[]{
		const propertiesMutation = super.mutation( original );
		const roleMutations = Acl.roleMutations( this.id, original.roles ?? [], this.roles );
		const permissionMutations = Permission.aclMutations( this.id, this.permissions ?? [], original.permissions  ?? [] );

		let groupMutations = super.childMutations( this, original.childGroups ?? [], this.childGroups! );
		groupMutations.forEach( m=>m.args = {memberId:m.args.id} );

		let users = super.childMutations( this, original.users ?? [], this.users );
		users.forEach( m=>m.args = {memberId:m.args.id} );
		return [...propertiesMutation, ...groupMutations, ...roleMutations, ...permissionMutations, ...users];
	}
	roles: Role[];
	childGroups: Group[];
	permissions: Permission[];
	users: User[];

	get properties():Partial<Group>{
		let properties:Partial<Group> = new Group(this);
		properties.roles=undefined;
		properties.childGroups=undefined;
		return properties;
	}
	static typeName = "Group";
}