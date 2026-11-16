package pes.gateway.protocol;

import com.fasterxml.jackson.annotation.JsonInclude;
import java.util.Collections;
import java.util.List;

@JsonInclude(JsonInclude.Include.NON_NULL)
public class CollabSyncEvent extends ServerEvent {
    private String roomId;
    private List<String> updates;
    private boolean historyFull;

    public CollabSyncEvent() {
        super("collab.sync");
        this.updates = Collections.emptyList();
    }

    public CollabSyncEvent(String roomId, List<String> updates) {
        this(roomId, updates, false);
    }

    public CollabSyncEvent(String roomId, List<String> updates, boolean historyFull) {
        super("collab.sync");
        this.roomId = roomId;
        this.updates = (updates != null) ? updates : Collections.emptyList();
        this.historyFull = historyFull;
    }

    public String getRoomId() {
        return roomId;
    }

    public void setRoomId(String roomId) {
        this.roomId = roomId;
    }

    public List<String> getUpdates() {
        return updates;
    }

    public void setUpdates(List<String> updates) {
        this.updates = updates;
    }

    public boolean isHistoryFull() {
        return historyFull;
    }

    public void setHistoryFull(boolean historyFull) {
        this.historyFull = historyFull;
    }
}
