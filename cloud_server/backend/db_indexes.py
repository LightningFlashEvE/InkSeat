"""Shared MongoDB index definitions for the backend and maintenance script."""

from __future__ import annotations


INDEX_DEFINITIONS = {
    'users': [
        ('username', 'username_unique', {'unique': True}),
        ('tokenHash', 'tokenHash_unique', {'unique': True, 'sparse': True}),
        ('registrationSlot', 'registrationSlot_unique', {'unique': True, 'sparse': True}),
    ],
    'service_admins': [
        ('username', 'username_unique', {'unique': True}),
        ('tokenHash', 'tokenHash_unique', {'unique': True, 'sparse': True}),
    ],
    'service_admin_audit': [
        ('createdAt', 'createdAt_idx', {}),
        ([('adminUsername', 1), ('createdAt', -1)], 'admin_createdAt_idx', {}),
        ([('targetType', 1), ('targetId', 1), ('createdAt', -1)], 'target_createdAt_idx', {}),
    ],
    'devices': [
        ('deviceId', 'deviceId_unique', {'unique': True}),
        ('owner', 'owner_idx', {}),
        ([('owner', 1), ('sortOrder', 1), ('addedAt', -1)], 'owner_sortOrder_addedAt_idx', {}),
        ('claimed', 'claimed_idx', {}),
    ],
    'device_status': [
        ('deviceId', 'deviceId_unique', {'unique': True}),
        ('lastSeen', 'lastSeen_idx', {}),
        ('unclaimedExpiresAt', 'unclaimedExpiresAt_ttl', {'expireAfterSeconds': 0}),
    ],
    'pages': [
        ('pageId', 'pageId_idx', {}),
        ([('owner', 1), ('pageId', 1)], 'owner_pageId_idx', {}),
        ('deviceId', 'deviceId_idx', {}),
        ([('deviceId', 1), ('name', 1)], 'deviceId_name_idx', {}),
        ([('deviceId', 1), ('updatedAt', -1)], 'deviceId_updatedAt_idx', {}),
    ],
    'pairing_codes': [
        ('deviceId', 'pairing_deviceId_unique', {'unique': True}),
        ('expiresAt', 'expiresAt_ttl', {'expireAfterSeconds': 0}),
    ],
    'saved_nameplate_templates': [
        (
            [('owner', 1), ('templateId', 1), ('baseTemplateId', 1)],
            'owner_template_base_unique',
            {'unique': True},
        ),
        ('owner', 'owner_idx', {}),
        ([('owner', 1), ('updatedAt', -1)], 'owner_updatedAt_idx', {}),
    ],
}


def _normalize_keys(keys):
    if isinstance(keys, str):
        return [(keys, 1)]
    return [(field, direction) for field, direction in keys]


def _index_matches(index, keys, options):
    index_keys = list(index.get('key', {}).items())
    if index_keys != _normalize_keys(keys):
        return False

    for option in ('unique', 'sparse', 'expireAfterSeconds'):
        expected = options.get(option)
        actual = index.get(option)
        if option in ('unique', 'sparse'):
            expected = bool(expected)
            actual = bool(actual)
        if expected != actual:
            return False
    return True


def ensure_index(collection, keys, preferred_name, **options):
    """Reuse an equivalent legacy index even when its name differs."""
    for index in collection.list_indexes():
        if _index_matches(index, keys, options):
            return index.get('name')
    return collection.create_index(keys, name=preferred_name, **options)


def ensure_all_indexes(db):
    created_or_reused = {}
    for collection_name, definitions in INDEX_DEFINITIONS.items():
        collection = db[collection_name]
        if collection_name == 'saved_nameplate_templates':
            # Older releases enforced templateId globally, although API lookups
            # are owner-scoped. Drop only that legacy single-field unique index.
            for index in list(collection.list_indexes()):
                if (
                    list(index.get('key', {}).items()) == [('templateId', 1)]
                    and bool(index.get('unique'))
                ):
                    collection.drop_index(index['name'])
        created_or_reused[collection_name] = []
        for keys, name, options in definitions:
            actual_name = ensure_index(collection, keys, name, **options)
            created_or_reused[collection_name].append(actual_name)
    return created_or_reused
