import { cloneClassArray, Mutation, SlugRow } from "jde-framework";
import { Group } from "./group";
import { Role } from "./role";
import { Permission } from "./permission";
import { Acl } from "./acl";

export type UserPK = number;

export class User extends SlugRow<User>{
	constructor( obj:any ){
		super("User", obj);
		let roles = obj.roles ?? obj.childRoles;

		this.distinguished = obj.distinguished;
		this.email = obj.email;
		this.expiration = obj.expiration;
		this.exponent = obj.exponent;
		this.fingerprint = obj.fingerprint;
		this.groups = cloneClassArray( obj.groups, Group );
		this.issuer = obj.issuer;
		this.loginName = obj.loginName;
		this.modulus = obj.modulus;
		this.password = obj.password;
		this.permissions = cloneClassArray( obj.permissionRights ?? obj.permissions, Permission ) ?? [];
		this.provider = obj.provider;
		this.subjectAlt = obj.subjectAlt;
		//"acl":[{"role":{"id":33,"name":"Opc Gateway Permissions","deleted":null},"identity":{"id":1}}]}
		if( obj.acl ){
			this.roles = [];
			this.permissions = [];
			for( let member of obj.acl ){
				if( member.role ){
					this.roles.push( new Role(member.role) );
				}else if( member.permissionRight ){
					this.permissions.push( new Permission(member.permissionRight) );
				}
			}
		}
		else
			this.roles = cloneClassArray( obj.roles, Role ) ?? [];
	}
	override equals( row:Partial<User> ):boolean{
		return super.equals(row)
			&& (this.email ?? "")==(row.email ?? "")//key-properties' Save enables on equals alone - an email edit there never enabled it
			&& JSON.stringify(this.groups)==JSON.stringify(row.groups)
			&& JSON.stringify(this.permissions)==JSON.stringify(row.permissions)
			&& JSON.stringify(this.roles)==JSON.stringify(row.roles);
	}
	//The form edits Provider, Email and Login name, and only slug, name and description went on the wire (reviews/m3-closing.md
	//#15):  a user added ahead of their first sign-in was stored with no provider and no login name, so the sign-in - which
	//looks the account up by both - missed it and made a second identity, and an Email edit saved nothing.  On create all three;
	//after that the email alone - the provider and the login name are what binds the account, and re-binding it is not an edit.
	//`providerId` and the numeric id the form's select holds:  the server looks an arg up by the column's member name, and
	//`provider` is not one - it is silently skipped.
	override mutationArgs( original:User ){
		const args:Record<string,unknown> = super.mutationArgs( original );
		if( !original?.id ){
			const providerId = Number( this.provider );
			if( this.provider!=undefined && `${this.provider}`!="" && Number.isInteger(providerId) )
				args["providerId"] = providerId;
			if( this.loginName )
				args["loginName"] = this.loginName;
			if( this.email )
				args["email"] = this.email;
		}
		else if( (this.email ?? "")!=(original.email ?? "") )
			args["email"] = this.email || null;//cleared:  null, not "" - the column is nullable
		return args as Partial<User>;
	}
	override mutation( original:User ):Mutation[]{
		console.assert( this.id==original.id, "User mutation id mismatch", this.id, original?.id );
		let propertyMutation = super.mutation( original );
		const permissionMutations = Permission.aclMutations( this.id, this.permissions, original?.permissions );
		const groupMutations = super.addRemoveMutations( Group.typeName, original.groups ?? [], this.groups, {memberId:this.id} );
		//createAcl( identity:{{ id:{} }}, role:{{id:{}}} )
		const roleMutations = Acl.roleMutations( this.id, original.roles ?? [], this.roles );
		return [...propertyMutation, ...permissionMutations, ...groupMutations, ...roleMutations];
	}
	distinguished:string;
	email:string;
	expiration:string;
	exponent:number;
	fingerprint:string;
	groups: Group[];
	issuer:string;
	loginName: string;
	modulus:number;
	password: string;
	permissions: Permission[];
	get properties():Partial<User>{ let properties:Partial<User> = new User(this); properties.roles=undefined; properties.permissions=undefined; properties.groups=undefined; return properties; }
	provider: string;
	roles: Role[];
	subjectAlt:string;
	override get collectionName():string{ return "users"; }
}