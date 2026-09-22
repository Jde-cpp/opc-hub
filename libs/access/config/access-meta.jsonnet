local common = import 'common-meta.libsonnet';
local types = common.types;
local sqlFunctions = common.sqlFunctions;
local smallSequenced = common.smallSequenced;
local pkSequenced = common.pkSequenced;
local valuesColumns = common.valuesColumns;
local valuesNK = common.valuesNK;
local slugColumns = common.slugColumns;
local slugNKs = common.slugNKs;
local defaultOps = ["Create", "Read", "Update", "Delete", "Purge", "Administer"];
{
	local tables = self.tables,
	views:{
		groupMembers:{
			comment: "Group Members",
			columns:{
				groupId: tables.groups.columns.identityId+{ criteria: null },
				groupSlug: slugColumns.slug,
				memberId: tables.groups.columns.memberId,
				isGroup: tables.identities.columns.isGroup
			}+slugColumns,
			naturalKeys: tables.identities.naturalKeys,
		},
		providersQL:{
			columns: {
				providerId: tables.providers.columns.providerId,
				providerTypeId: tables.providers.columns.providerTypeId,
				name: tables.providers.columns.slug+{ comment: "if provider_id=OpcServer: the specific opcServer slug, otherwise providers[provider_id].name " },
			},
			naturalKeys: [["provider_id", "name"]]
		},
		usersQL:{
			columns: {
				identityId: types.uint+{ sk: 0, i: 0 },
				name: tables.identities.columns.name,
				attributes: tables.identities.columns.attributes,
				created: tables.identities.columns.created,
				updated: tables.identities.columns.updated,
				deleted: tables.identities.columns.deleted,
				slug: tables.identities.columns.slug,
				description: tables.identities.columns.description,
				providerId: tables.identities.columns.providerId,
				email: tables.identities.columns.email,
				loginName: tables.users.columns.loginName,
				modulus: tables.users.columns.modulus,
				exponent: tables.users.columns.exponent,
				issuer: tables.users.columns.issuer,
				subjectAlt: tables.users.columns.subjectAlt,
				distinguished: tables.users.columns.distinguished,
				expiration: tables.users.columns.expiration,
				fingerprint: tables.users.columns.fingerprint
			},
			naturalKeys: tables.identities.naturalKeys,
		}
	},
	tables:{
		identities:{
			comment: "Group or User",
			columns:{
				identityId: pkSequenced,
				providerId: tables.providers.columns.providerId+{ pkTable: "providers", nullable:true, i: 15, sk:null },
				isGroup: types.bit+{ default: false, i: 101 },
				email: types.varchar+{ length: 256, nullable: true, comment: "cert SAN rfc822 at key enrollment, login email for Google", i: 110 },//not in slugColumns - roles shares that block.
			}+slugColumns,
			naturalKeys:[ ["name","provider_id"], ["slug"] ],
			ops: ["None"]
		},
		users:{
			columns: {
				identityId: tables.identities.columns.identityId+{ pkTable:"identities", criteria: {columnName:"is_group", value: false} },
				loginName: valuesColumns.name+{ nullable: true },
				password: types.varbinary+{ length: 2048, encrypted:true, nullable: true, i:101 },
				modulus: types.varchar+{ length: 2048, nullable:true, comment: "Used for RSA", i:102 },
				exponent: types.uint+{ nullable:true, comment: "Used for RSA", i:103 },
				issuer: types.varchar+{ length: 1024, nullable: true, insertable: false, updateable: false, comment: "cert issuer DN (RFC2253) - key enrollment", i:104 },
				subjectAlt: types.varchar+{ length: 1024, nullable: true, insertable: false, updateable: false, comment: "cert subjectAltName, openssl config syntax (URI:…,DNS:…) - key enrollment", i:105 },
				distinguished: types.varchar+{ length: 1024, nullable: true, insertable: false, updateable: false, comment: "cert subject DN (RFC2253) - key enrollment", i:106 },
				expiration: types.dateTime+{ nullable: true, insertable: false, updateable: false, comment: "cert notAfter - key enrollment", i:107 },
				fingerprint: types.varchar+{ length: 95, nullable: true, insertable: false, updateable: false, comment: "cert sha-256 fingerprint, colon hex as openssl prints it - key enrollment", i:108 }
			},
			ops: ["Create", "Read", "Update", "Delete", "Purge", "Administer", "Execute", "Subscribe"],
			extends: "identities",
			purgeProc: "user_purge", //profiles, acl and group rows reference the identity with no cascade - the proc takes them first (access-review3 #14).
			qlView: "users_ql"
		},
		groups:{
			columns: {
				identityId: tables.identities.columns.identityId+{ pkTable: "identities", criteria: {columnName:"is_group", value: true} },
				memberId: 	tables.identities.columns.identityId+{ pkTable: "identities", name: "member_id", sk: 1, i:1 },
			},
			map: {parentId:"identity_id", childId:"member_id"},
			extends: "identities",
			purgeProc: "group_purge", //acl grants and the group's rows on both sides of access_groups reference the identity with no cascade - the proc takes them first, as users' does.
		},
		providerTypes:{
			columns: {
				providerTypeId: types.uint8+{sk:0, i:0},
			}+valuesColumns
		},
		providers:{
			columns: {
				providerId: smallSequenced,
				providerTypeId: tables.providerTypes.columns.providerTypeId+{ sk:null, pkTable: "provider_types", i:1 },
				slug: slugColumns.slug+{ nullable: true, comment: "Points to slug in another table (eg OpcServer)" }
			},
			naturalKeys:[["provider_type_id","slug"]],
			purgeProc: "provider_purge",
			qlView: "providers_ql",
			ops: ["None"]
		},
		resources:{
			columns: {
				resourceId: smallSequenced,
				schemaName: types.varchar+{ length: 32, i:1 },
				name: types.varchar+{ length: 64, i:10 },
				slug:types.varchar+{ length: 32, i:20 },
				attributes: types.uint16+{ nullable: true, i:30 },
				created: types.dateTime+{ insertable: false, updateable: false, default: sqlFunctions.now.name, i:40 },
				updated: types.dateTime+{ nullable: true, insertable: false, updateable: false, i:50 },
				deleted:types.dateTime+{ nullable: true, insertable: false, updateable: false, i:60 },
				description: types.varchar+{ length: 2048, nullable: true, i:70 },
				criteria: types.varchar+{ nullable: true, length:672, i:100 },
				allowed: types.ulong+{ pkTable: "rights", i:101, nullable:true, comment: "available rights for this resource" }
			},
			ops: ["Delete", "Subscribe"],
			naturalKeys: [["schema_name", "slug", "criteria"]],
		},
		permissions:{
			columns: {
				permissionId: pkSequenced,
				isRole: types.bit+{ default: false, i: 101 },
			},
			ops: ["None"]
		},
		permissionRights:{
			columns: {
				permissionId: tables.permissions.columns.permissionId+{ pkTable: "permissions", i:0, sk:0 },
				resourceId: tables.resources.columns.resourceId+{ sk:null, pkTable: "resources", i:1 },
				allowed: types.ulong+{ pkTable: "rights", i:2 },
				denied: types.ulong+{ pkTable: "rights", i:3 },
			},
			ops: ["None"]
		},
		roles:{
			columns: {
				roleId: tables.permissions.columns.permissionId+{ insertable:false, pkTable: "permissions", i:0, sk:0 },
			}+slugColumns,
			customInsertProc: true,
			purgeProc: "role_purge",
			addProc: "role_add",
			removeProc: "role_remove",
			naturalKeys: slugNKs,
			ops: ["Create", "Read", "Update", "Delete", "Purge", "Administer", "Subscribe"],
		},
		roleMembers:{
			columns: {
				roleId: tables.permissions.columns.permissionId+{ pkTable: "roles", sk: 0, i:0 },
				memberId: tables.permissions.columns.permissionId+{ pkTable: "permissions", sk:1, i:1 },
			},
			extends: "roles",
			map: {parentId:"role_id", childId:"member_id"},
			ops: ["None"]
		},
		rights:{
			columns: {
				rightId: types.uint8+{ sk:0, i:0 },
				name: types.varchar+{ length: 11, i:1 }
			},
			ops: ["None"],
			isFlags: true
		},
		acl:{
			columns: {
				identityId: tables.identities.columns.identityId+{ pkTable: {name:"identities"}, sk: 0, i:0 },
				permissionId: tables.permissions.columns.permissionId+{ pkTable: "permissions", sk: 1, i:1 }
			},
			customInsertProc: true,
			map:: { parentId:"identity_id", childId:"permission_id" },//not a map, but connector.
			ops: ["Read", "Administer", "Subscribe"] //the mutations gate on TestAdmin of the target resource; Read is what AclQLSelectAwait gates on, and with no ops ResourceSync never made a row for it to gate with, so the acl was enumerable by anyone (access-review3 #21).  Created disabled like every synced resource - restore it to enforce.
		},
		seeds:{
			comment: "The seed files (.roles) applied, by content - a start whose file is unchanged skips it, so an admin's edits to a seeded role survive (reviews/m3-closing.md #12)",
			columns: {
				name: types.varchar+{ length: 256, sk: 0, i:0, comment: "the file's name, e.g. access.roles" },
				contentHash: types.varchar+{ length: 64, i:1, comment: "md5 of the text last applied" },
				applied: types.dateTime+{ i:2 }
			},
			ops: ["None"]
		},
		profiles:{
			comment: "Per-user UI profile blobs, keyed by page/component",
			columns: {
				identityId: types.uint+{ pkTable: "users", sk: 0, i:0 },
				url: slugColumns.slug+{ sk: 1, i:1, comment: "profile key, e.g. 'favorites', 'logs/views'" },
				value: types.varchar+{ length: 4096, i:2 }
			},
			ops: ["None"] //scoped to the executer in Server::CustomQuery/CustomMutation.
		}
	}
}