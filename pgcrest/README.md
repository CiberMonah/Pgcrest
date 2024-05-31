# Minimal REST-to-PostgreSQL bridge

This is a minimal RESTful web interface written in C and libuv.
Most ideas is taken form [PostgREST](https://postgrest.org), but the philosophy of **PGCREST** is to be as thin as possible.
It tries to keep the code simple as fast.

## Requirements
The following packages are required to build and run
-  `libhttp-parser-dev` simple http request and response parser
-  `libuv-dev` package is needed to be installed for compilation
-  `libjansson` - JSON library for C.
-  `libjwt` - for JWT-based authorization
-  `libiniparser` -- for config parsing
-  `libpq-dev` -- developent library for PostgreSQL clients

For Ubuntu-based Linux distributions all required development libraries (besides build-essentials)  can be installed with the command:
```sh
sudo apt install libhttp-parser-dev libuv1-dev libjansson-dev libiniparser-dev libjwt-dev libpq-dev
```

## Build
Just `make && make install`

If you're on BSD, use `gmake` instead of `make`

## Authentication

The authentication in **PGCREST** is done with JWT - [JSON Web Tokens](https://jwt.io).
JWT is a simple method of checking the request is coming from an authorized source.

The JWT token is normally passed in `Authorization: Bearer <JWT>`  HTTP-header.
**PGCREST** verifies the signature with respect to the [key](#section-jwt), looks at the *payload* of the JWT
and extracts the `exp` and `role` fields.

The `role` field value is taken from the JWT *payload* and passed in the beginning of SQL *Transaction* with the following impersonating command:
```SQL
set local role <role>;
```

If there is no the `Authorization:` HTTP-header in the request, the default `anonrole` (see [sql](#section-sql) section of the configuration file) is used  for impersonation.

All the roles used in differentiating API rights shoud be granted to the main *authenticator* role (see [pgpool](#section-pgpool) section of configuration) which is used in each SQL-connection.
This can be done with the following command:
```SQL
grant <role> to <authenticator>;
``` 

## Request Mapping

The HTTP-request consists of 4 main parts: METHOD, PATH, QUERY and BODY. Additionally there may be some headers used for hints (such as `Accept`).

The SQL request consists of *vertical* filter (list of specific columns), relation or function name, parameters and *horizontal* filter.

### Method mapping

The METHOD is mapped to the relation or function prefix. F.e.:
```
GET /some_table
```
maps to 
```sql
SELECT * from get_some_table
```
or 
```
PATCH /some_table
```
maps to 
```sql
SELECT * from patch_some_table
``` 

### Body mapping

If the BODY exists, it is implied that the PATH addresses the *function* and the whole BODY is passed as the only function argument:
```
POST /some_func
Content-Type: application/json

{"some": "example"}
```
maps to
```sql
SELECT * from post_some_func('{"some": "example"}'::jsonb)
```

### Query mapping

Some QUERY params are mapped in the following way.
If the QUERY contains `select` parameter, such as `select=a,b,c` it is interpreted as the list of columns in the SELECT clause:
```
GET /some_table?select=a,b
```
maps to
```sql
select a, b from get_some_table;
```

## Minimal Setup

1. Install **PGCREST**
2. Create roles for a database, f.e. `test1`:
3. Set the following config
4. Start `pgcrest`
5. Run `curl`

## Authorization and JWT issuing Setup

1. Install `pgjwt` extension
2. Write simple JWT-generation function
3. Return JWT token.
4. Run `curl` to request JWT token.
5. Run `curl` with option `-H "Authorization: Bearer <JWT>"`
	


## Configuration

The config file resides in `pgcrest.conf`. It is in `.ini` format.

### Section jwt
```
[jwt]
key = <jwt-key>
```
The key used to verify JWT token.



### Section auth
```
[]a
basic-auth-proc = <auth_procedure_name> # default: basic_auth
```

Where `<auth_procedure_name>` is the stored procedure for basic authentication. This procedure should take the
`<params>` field from the `Authorization: Basic <params>` Header  (if it is) and should return JWT token if
authorization succeeds and raise an exception otherwise.

```
[auth]
bearer-auth-proc = <auth-proc-name> #default:  bearer_auth
```
Name of the procedure to authorize through the `Authorization: Bearer ...` header.
Should return name  of the role in case of success or raise and exception otherwise.

### Section pgpool
```
[pgpool]
connstr = <conn_string>
```
PostgreSQL database connection string

```
[pgpool]
schema = <default_schema>
```
Default database schema to use.


```
[pgpool]
maxconn = <max connections>
```
Maximum number of connections in the pool. Default value: 10.

```
[pgpool]
timeout = <timeout in seconds>
```
Number of seconds active connection remains in busy state since the last activity. Connection becomes free after this timeout. Default value: 10.

```
[main]
authorized-role = <rolename>
```
The role to  impersonate into for authorized users.

```
[sql]
anonrole = <rolename>
```
The role to impersonate into for anonymous users.


```
[sql]
pre-exec = <function-name>
```
The function to be executed before each processing. Some access checks or validations can be done there.
The function should return `void` and raise an exception in case of failure of checks.

## Roadmap

Urgent:

1. Parse exception and return a proper JSON response with 500 status.

2. Systemd Unit. Run as a service with pre-forked workers (the numbers of workers is configurable). 


The following things should be added in the nearest time:

1. Load database schema and parse it:
	
	- *functions*: number of args, their types and return type
	- *tables/views*:  types of columns

2. Guess the response `content-type` based on procedure return type
3. Readonly and write transactions based on HTTP-method.

Not so far future:

1. Automatically understand what type of sql object is requested: *table*/*view* or *procedure*.

