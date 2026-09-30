# Underwater movement data follow-up

`UMSMobileSuitData::bAmphibious` defaults to `false`. No `.uasset` was modified as part of the
source-only underwater movement implementation.

After the map/editor work is complete, review the existing mobile-suit data assets and enable
`bAmphibious` for suits whose names or design indicate aquatic operation (for example, Aqua,
Marine, or underwater variants). The suit-detail UI currently renders a fixed `[地上]` badge;
a later data/UI pass should derive that badge from `bAmphibious` and display the appropriate
ground/amphibious label.
